#include "nexus/module/network_center/network_repository.hpp"

#include "nexus/db/database.hpp"
#include "nexus/db/migration.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <span>

using namespace nexus::module::network_center;
using nexus::core::now;

namespace {
nexus::db::Database migrated_db() {
    auto db = nexus::db::Database::open_in_memory();
    nexus::db::migrate(db, "network_center", network_center_migrations());
    return db;
}
} // namespace

TEST_CASE("network add / find / delete", "[network_center][repo]") {
    auto db = migrated_db();
    NetworkRepository repo(db);

    REQUIRE(repo.networks().empty());

    const auto id = repo.add_network("192.168.1.0/24", "Home LAN");
    REQUIRE(repo.networks().size() == 1);

    const auto found = repo.find_network(id);
    REQUIRE(found.has_value());
    REQUIRE(found->cidr == "192.168.1.0/24");
    REQUIRE(found->label == "Home LAN");

    REQUIRE(repo.delete_network(id));
    REQUIRE_FALSE(repo.delete_network(id));
    REQUIRE(repo.networks().empty());
}

TEST_CASE("upsert_device inserts then refreshes on repeat discovery", "[network_center][repo]") {
    auto db = migrated_db();
    NetworkRepository repo(db);
    const auto network_id = repo.add_network("192.168.1.0/24", "Home LAN");
    const auto t0 = now();

    const auto id = repo.upsert_device(network_id, "192.168.1.10", "", t0);
    REQUIRE(repo.devices(network_id).size() == 1);

    const auto first = repo.find_device(id);
    REQUIRE(first.has_value());
    REQUIRE(first->status == "online");
    REQUIRE(first->hostname.empty());

    const auto t1 = t0 + std::chrono::minutes{5};
    const auto again = repo.upsert_device(network_id, "192.168.1.10", "printer.local", t1);
    REQUIRE(again == id);
    REQUIRE(repo.devices(network_id).size() == 1);

    const auto refreshed = repo.find_device(id);
    REQUIRE(refreshed->hostname == "printer.local");
    REQUIRE(nexus::core::to_iso8601(refreshed->last_seen_at) == nexus::core::to_iso8601(t1));
    REQUIRE(nexus::core::to_iso8601(refreshed->first_seen_at) ==
           nexus::core::to_iso8601(first->first_seen_at));
}

TEST_CASE("set_device_status updates status and last_seen_at only when online",
         "[network_center][repo]") {
    auto db = migrated_db();
    NetworkRepository repo(db);
    const auto network_id = repo.add_network("192.168.1.0/24", "Home LAN");
    const auto t0 = now();
    const auto id = repo.upsert_device(network_id, "192.168.1.10", "", t0);

    const auto t1 = t0 + std::chrono::minutes{1};
    repo.set_device_status(id, "offline", t1);
    auto device = repo.find_device(id);
    REQUIRE(device->status == "offline");
    // unchanged: not seen while offline
    REQUIRE(nexus::core::to_iso8601(device->last_seen_at) == nexus::core::to_iso8601(t0));

    const auto t2 = t1 + std::chrono::minutes{1};
    repo.set_device_status(id, "online", t2);
    device = repo.find_device(id);
    REQUIRE(device->status == "online");
    REQUIRE(nexus::core::to_iso8601(device->last_seen_at) == nexus::core::to_iso8601(t2));
}

TEST_CASE("checks and check_results round-trip", "[network_center][repo]") {
    auto db = migrated_db();
    NetworkRepository repo(db);
    const auto network_id = repo.add_network("192.168.1.0/24", "Home LAN");
    const auto t0 = now();
    const auto device_id = repo.upsert_device(network_id, "192.168.1.10", "", t0);

    const auto check_id = repo.begin_check(network_id, "monitor", t0);
    repo.record_check_result(check_id, device_id, "192.168.1.10", "online",
                             std::chrono::microseconds{1500}, t0);
    repo.finish_check(check_id, 1, t0);

    const auto results = repo.recent_results(device_id);
    REQUIRE(results.size() == 1);
    REQUIRE(results[0].status == "online");
    REQUIRE(results[0].rtt.has_value());
    REQUIRE(results[0].rtt->count() == 1500);
}

TEST_CASE("the v1->v2 devices.open_ports column add preserves existing rows",
         "[network_center][repo][migration]") {
    auto db = nexus::db::Database::open_in_memory();
    const auto all_migrations = network_center_migrations();
    REQUIRE(all_migrations.size() >= 2);
    nexus::db::migrate(db, "network_center", all_migrations.subspan(0, 1));
    REQUIRE(nexus::db::schema_version(db, "network_center") == 1);

    // Seed directly via SQL matching the v1 schema (no open_ports column
    // yet) - NetworkRepository's own queries are always v2-shaped, so they
    // can't be used against a deliberately-not-yet-migrated database; this
    // is standing in for a real pre-upgrade install's on-disk data.
    db.execute(
        "INSERT INTO networks (id, cidr, label, created_at) VALUES "
        "(1, '192.168.1.0/24', 'Home LAN', '2026-01-01T00:00:00Z');"
        "INSERT INTO devices (id, network_id, address, hostname, status, first_seen_at, "
        "last_seen_at) VALUES (1, 1, '192.168.1.10', 'printer.local', 'online', "
        "'2026-01-01T00:00:00Z', '2026-01-01T00:00:00Z');");

    nexus::db::migrate(db, "network_center", all_migrations);
    // Not hardcoded as a literal version number - a later migration (e.g.
    // Phase 6's mac column) legitimately moves this forward, and this test
    // only cares that migrating all the way lands on the newest version,
    // not any specific one.
    REQUIRE(nexus::db::schema_version(db, "network_center") ==
           all_migrations.back().version);

    NetworkRepository repo(db);
    const auto device = repo.find_device(1);
    REQUIRE(device.has_value());
    REQUIRE(device->hostname == "printer.local");
    REQUIRE(device->open_ports.empty()); // new column, default ''
    REQUIRE(device->mac.empty()); // new column, default ''

    repo.set_device_open_ports(1, "80,443");
    REQUIRE(repo.find_device(1)->open_ports == "80,443");
}

TEST_CASE("set_device_open_ports round-trips and defaults to empty",
         "[network_center][repo]") {
    auto db = migrated_db();
    NetworkRepository repo(db);
    const auto network_id = repo.add_network("192.168.1.0/24", "Home LAN");
    const auto id = repo.upsert_device(network_id, "192.168.1.10", "", now());

    REQUIRE(repo.find_device(id)->open_ports.empty());

    repo.set_device_open_ports(id, "80,443");
    REQUIRE(repo.find_device(id)->open_ports == "80,443");

    repo.set_device_open_ports(id, "");
    REQUIRE(repo.find_device(id)->open_ports.empty());
}

TEST_CASE("rename_device and delete_device", "[network_center][repo]") {
    auto db = migrated_db();
    NetworkRepository repo(db);
    const auto network_id = repo.add_network("192.168.1.0/24", "Home LAN");
    const auto id = repo.upsert_device(network_id, "192.168.1.10", "", now());

    repo.rename_device(id, "My NAS");
    REQUIRE(repo.find_device(id)->label == "My NAS");

    REQUIRE(repo.delete_device(id));
    REQUIRE_FALSE(repo.find_device(id).has_value());
}
