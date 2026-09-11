#include "nexus/module/network_center/device_monitor.hpp"

#include "nexus/db/database.hpp"
#include "nexus/db/migration.hpp"
#include "nexus/module/network_center/network_repository.hpp"
#include "nexus/notify/notification_center.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <unordered_map>

using namespace nexus::module::network_center;

namespace {

nexus::db::Database migrated_db() {
    auto db = nexus::db::Database::open_in_memory();
    nexus::db::migrate(db, "network_center", network_center_migrations());
    return db;
}

// A scriptable ping: returns online for every device unless its address is in `down`.
struct ScriptedPing {
    std::shared_ptr<std::unordered_map<std::string, bool>> down =
        std::make_shared<std::unordered_map<std::string, bool>>();

    PingReading operator()(const Device& device) const {
        PingReading reading;
        if ((*down)[device.address]) {
            reading.ok = false;
        } else {
            reading.ok = true;
            reading.rtt = std::chrono::microseconds{2000};
        }
        return reading;
    }
};

} // namespace

TEST_CASE("device monitor records a check result per known device each tick",
         "[network_center][monitor]") {
    auto db = migrated_db();
    NetworkRepository seed(db);
    const auto network_id = seed.add_network("192.168.1.0/24", "Home LAN");
    const auto device_id = seed.upsert_device(network_id, "192.168.1.10", "", nexus::core::now());

    nexus::notify::NotificationCenter notifications;
    ScriptedPing ping;
    DeviceMonitor monitor(std::make_unique<NetworkRepository>(db), notifications, ping);

    monitor.tick();
    REQUIRE(monitor.ticks() == 1);

    NetworkRepository repo(db);
    const auto results = repo.recent_results(device_id);
    REQUIRE(results.size() == 1);
    REQUIRE(results[0].status == "online");
    REQUIRE(repo.find_device(device_id)->status == "online");
}

TEST_CASE("device monitor notifies on offline and online transitions",
         "[network_center][monitor]") {
    auto db = migrated_db();
    NetworkRepository seed(db);
    const auto network_id = seed.add_network("192.168.1.0/24", "Home LAN");
    const auto device_id = seed.upsert_device(network_id, "192.168.1.10", "", nexus::core::now());

    nexus::notify::NotificationCenter notifications;
    ScriptedPing ping;
    DeviceMonitor monitor(std::make_unique<NetworkRepository>(db), notifications, ping);

    monitor.tick(); // first tick: online, no transition (no prior status)
    REQUIRE(notifications.size() == 0);

    (*ping.down)["192.168.1.10"] = true;
    monitor.tick(); // transition to offline
    REQUIRE(notifications.size() == 1);
    REQUIRE(notifications.recent()[0].severity == nexus::notify::Severity::Warning);

    monitor.tick(); // still offline: no new notification
    REQUIRE(notifications.size() == 1);

    (*ping.down)["192.168.1.10"] = false;
    monitor.tick(); // back online
    REQUIRE(notifications.size() == 2);
    REQUIRE(notifications.recent()[0].severity == nexus::notify::Severity::Info);

    NetworkRepository repo(db);
    REQUIRE(repo.find_device(device_id)->status == "online");
}

TEST_CASE("device monitor with no known devices does nothing", "[network_center][monitor]") {
    auto db = migrated_db();
    nexus::notify::NotificationCenter notifications;
    ScriptedPing ping;
    DeviceMonitor monitor(std::make_unique<NetworkRepository>(db), notifications, ping);

    monitor.tick();
    REQUIRE(notifications.size() == 0);
}

TEST_CASE("inactive device monitor does nothing", "[network_center][monitor]") {
    auto db = migrated_db();
    NetworkRepository seed(db);
    const auto network_id = seed.add_network("192.168.1.0/24", "Home LAN");
    seed.upsert_device(network_id, "192.168.1.10", "", nexus::core::now());

    nexus::notify::NotificationCenter notifications;
    ScriptedPing ping;
    DeviceMonitor monitor(std::make_unique<NetworkRepository>(db), notifications, ping);
    monitor.set_active(false);

    monitor.tick();
    REQUIRE(monitor.ticks() == 0);
}
