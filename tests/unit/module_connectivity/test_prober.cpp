#include "nexus/module/connectivity/prober.hpp"

#include "nexus/db/database.hpp"
#include "nexus/db/migration.hpp"
#include "nexus/module/connectivity/connectivity_repository.hpp"
#include "nexus/notify/notification_center.hpp"
#include "nexus/services/events/events.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

using namespace nexus::module::connectivity;

namespace {

nexus::db::Database migrated_db() {
    auto db = nexus::db::Database::open_in_memory();
    nexus::db::migrate(db, "connectivity", connectivity_migrations());
    return db;
}

// A scriptable probe: returns "ok" for a target unless it is in `down`.
struct ScriptedProbe {
    std::shared_ptr<std::unordered_map<std::string, bool>> down =
        std::make_shared<std::unordered_map<std::string, bool>>();

    ProbeReading operator()(const ProbeTarget& target) const {
        ProbeReading r;
        if ((*down)[target.id]) {
            r.status = "timeout";
            r.detail = "scripted down";
        } else {
            r.status = "ok";
            r.rtt = std::chrono::microseconds{5000};
        }
        return r;
    }
};

// Fixed gateway-check fakes so no test in this file ever touches real
// network I/O - default_gateway_check() (nexus::net::default_gateway +
// icmp_ping) is only ever invoked in production and in libs/net's own
// tests. Every Prober construction below passes one explicitly instead of
// relying on the constructor's default argument.
GatewayCheck reachable_gateway() { return {"192.168.1.1", true}; }
GatewayCheck unreachable_gateway() { return {"192.168.1.1", false}; }
GatewayCheck no_gateway_found() { return {std::nullopt, false}; }

// Marks every currently-seeded target down, not just the original two -
// connectivity_migrations() has grown since these tests were first written
// (a 'dns-check' target was added in a later migration), and "every target
// failing" has to mean literally every one for the total-outage path below
// to trigger at all.
void mark_all_down(ConnectivityRepository& repo, ScriptedProbe& probe) {
    for (const auto& target : repo.targets()) {
        (*probe.down)[target.id] = true;
    }
}

} // namespace

TEST_CASE("prober records one sample per enabled target each tick", "[connectivity][prober]") {
    auto db = migrated_db();
    nexus::notify::NotificationCenter notifications;
    ScriptedProbe probe;

    Prober prober(std::make_unique<ConnectivityRepository>(db), notifications, probe,
                  /*outage_after=*/2, std::chrono::hours{24 * 30}, &reachable_gateway);
    prober.tick();

    ConnectivityRepository repo(db);
    const auto since = nexus::core::now() - std::chrono::minutes{1};
    REQUIRE(repo.samples_since("google-dns", since).size() == 1);
    REQUIRE(repo.samples_since("cloudflare-dns", since).size() == 1);
    REQUIRE(prober.ticks() == 1);
}

TEST_CASE("an outage opens after N failures and closes on recovery", "[connectivity][prober]") {
    auto db = migrated_db();
    nexus::notify::NotificationCenter notifications;
    ScriptedProbe probe;

    Prober prober(std::make_unique<ConnectivityRepository>(db), notifications, probe,
                  /*outage_after=*/2, std::chrono::hours{24 * 30}, &reachable_gateway);
    ConnectivityRepository repo(db);

    (*probe.down)["google-dns"] = true;

    prober.tick(); // failure 1 - no outage yet
    REQUIRE_FALSE(repo.open_outage("google-dns").has_value());
    REQUIRE(notifications.size() == 0);

    prober.tick(); // failure 2 - outage opens
    REQUIRE(repo.open_outage("google-dns").has_value());
    REQUIRE(notifications.size() == 1);
    REQUIRE(notifications.recent()[0].severity == nexus::notify::Severity::Warning);

    prober.tick(); // still down - bumps, no new notification
    REQUIRE(notifications.size() == 1);

    (*probe.down)["google-dns"] = false;
    prober.tick(); // recovery
    REQUIRE_FALSE(repo.open_outage("google-dns").has_value());
    REQUIRE(notifications.size() == 2);
    REQUIRE(notifications.recent()[0].severity == nexus::notify::Severity::Info);

    const auto outages = repo.recent_outages();
    REQUIRE(outages.size() == 1);
    REQUIRE(outages[0].samples_failed == 2); // failure 2 (open) + failure 3 (bump)
    REQUIRE(outages[0].ended_at.has_value());
}

TEST_CASE("inactive prober does nothing", "[connectivity][prober]") {
    auto db = migrated_db();
    nexus::notify::NotificationCenter notifications;
    Prober prober(std::make_unique<ConnectivityRepository>(db), notifications, ScriptedProbe{},
                  /*outage_after=*/2, std::chrono::hours{24 * 30}, &reachable_gateway);

    prober.set_active(false);
    prober.tick();

    ConnectivityRepository repo(db);
    REQUIRE(repo.samples_since("google-dns", nexus::core::now() - std::chrono::minutes{1}).empty());
}

TEST_CASE("every target failing with a reachable gateway is classified as beyond-router",
         "[connectivity][prober][gateway]") {
    auto db = migrated_db();
    ConnectivityRepository repo(db);
    nexus::notify::NotificationCenter notifications;
    ScriptedProbe probe;
    mark_all_down(repo, probe);

    Prober prober(std::make_unique<ConnectivityRepository>(db), notifications, probe,
                  /*outage_after=*/1, std::chrono::hours{24 * 30}, &reachable_gateway);
    prober.tick();

    const auto recent = notifications.recent();
    const bool found = std::any_of(recent.begin(), recent.end(), [](const auto& note) {
        return note.title == "Internet unreachable (your router is fine)";
    });
    REQUIRE(found);

    const auto status = repo.latest_path_status();
    REQUIRE(status.has_value());
    REQUIRE(status->status == PathStatus::BeyondRouter);
}

TEST_CASE("every target failing with an unreachable gateway is classified as a local issue",
         "[connectivity][prober][gateway]") {
    auto db = migrated_db();
    ConnectivityRepository repo(db);
    nexus::notify::NotificationCenter notifications;
    ScriptedProbe probe;
    mark_all_down(repo, probe);

    Prober prober(std::make_unique<ConnectivityRepository>(db), notifications, probe,
                  /*outage_after=*/1, std::chrono::hours{24 * 30}, &unreachable_gateway);
    prober.tick();

    const auto recent = notifications.recent();
    const bool found = std::any_of(recent.begin(), recent.end(),
                                   [](const auto& note) { return note.title == "Local network issue"; });
    REQUIRE(found);

    const auto status = repo.latest_path_status();
    REQUIRE(status.has_value());
    REQUIRE(status->status == PathStatus::LocalIssue);
}

TEST_CASE("every target failing with no discoverable gateway still notifies once",
         "[connectivity][prober][gateway]") {
    auto db = migrated_db();
    ConnectivityRepository repo(db);
    nexus::notify::NotificationCenter notifications;
    ScriptedProbe probe;
    mark_all_down(repo, probe);

    Prober prober(std::make_unique<ConnectivityRepository>(db), notifications, probe,
                  /*outage_after=*/1, std::chrono::hours{24 * 30}, &no_gateway_found);
    prober.tick();
    prober.tick(); // second consecutive total outage: no duplicate notification

    const auto recent = notifications.recent();
    const auto total_outage_notes =
        std::count_if(recent.begin(), recent.end(),
                      [](const auto& note) { return note.title == "Internet unreachable"; });
    REQUIRE(total_outage_notes == 1);

    const auto status = repo.latest_path_status();
    REQUIRE(status.has_value());
    REQUIRE(status->status == PathStatus::NoGatewayFound);
}

TEST_CASE("recovery from a total outage posts a restored notification once",
         "[connectivity][prober][gateway]") {
    auto db = migrated_db();
    ConnectivityRepository repo(db);
    nexus::notify::NotificationCenter notifications;
    ScriptedProbe probe;
    mark_all_down(repo, probe);

    Prober prober(std::make_unique<ConnectivityRepository>(db), notifications, probe,
                  /*outage_after=*/1, std::chrono::hours{24 * 30}, &reachable_gateway);
    prober.tick(); // total outage: classified + notified

    for (const auto& target : repo.targets()) {
        (*probe.down)[target.id] = false;
    }
    prober.tick(); // recovery

    const auto recent = notifications.recent();
    const bool restored = std::any_of(
        recent.begin(), recent.end(),
        [](const auto& note) { return note.title == "Connectivity restored"; });
    REQUIRE(restored);

    const auto status = repo.latest_path_status();
    REQUIRE(status.has_value());
    REQUIRE(status->status == PathStatus::AllOk);
}

// Spec section 9 hook #3 (Connectivity->Backup pause-on-outage): Prober
// publishes ConnectivityStateEvent on the same edges as the notifications
// above, so Backup can pause network destinations without polling anything.
TEST_CASE("prober publishes a ConnectivityStateEvent on total-outage transitions",
         "[connectivity][prober][gateway][events]") {
    auto db = migrated_db();
    ConnectivityRepository repo(db);
    nexus::notify::NotificationCenter notifications;
    nexus::services::EventBus events;
    ScriptedProbe probe;
    mark_all_down(repo, probe);

    std::vector<bool> seen;
    events.subscribe<nexus::services::events::ConnectivityStateEvent>(
        [&seen](const nexus::services::events::ConnectivityStateEvent& e) {
            seen.push_back(e.internet_reachable);
        });

    Prober prober(std::make_unique<ConnectivityRepository>(db), notifications, probe,
                  /*outage_after=*/1, std::chrono::hours{24 * 30}, &reachable_gateway, &events);
    prober.tick(); // total outage: publishes internet_reachable=false
    REQUIRE(seen == std::vector<bool>{false});

    for (const auto& target : repo.targets()) {
        (*probe.down)[target.id] = false;
    }
    prober.tick(); // recovery: publishes internet_reachable=true
    REQUIRE(seen == std::vector<bool>{false, true});
}

TEST_CASE("a null EventBus is safe - Prober only notifies, never publishes",
         "[connectivity][prober][gateway][events]") {
    auto db = migrated_db();
    ConnectivityRepository repo(db);
    nexus::notify::NotificationCenter notifications;
    ScriptedProbe probe;
    mark_all_down(repo, probe);

    Prober prober(std::make_unique<ConnectivityRepository>(db), notifications, probe,
                  /*outage_after=*/1, std::chrono::hours{24 * 30}, &reachable_gateway,
                  /*events=*/nullptr);
    prober.tick(); // must not crash with no EventBus supplied

    const auto recent = notifications.recent();
    const bool found = std::any_of(recent.begin(), recent.end(),
                                   [](const auto& note) { return note.title == "Internet unreachable (your router is fine)"; });
    REQUIRE(found);
}

// UFR-010: retention is threaded from the caller into Prober and actually
// gates what prune_before() removes - closes the "Known gaps" note in
// docs/UFR_CONFORMANCE.md about the settings value reaching a real prune.
TEST_CASE("prober prunes samples older than its configured retention",
         "[connectivity][prober][retention]") {
    auto db = migrated_db();
    ConnectivityRepository repo(db);

    const auto now = nexus::core::now();
    // Seeded directly (bypassing tick(), which always stamps "now") so it
    // predates the prober's own inserts and falls outside a 24h retention.
    ConnectivitySample old_sample;
    old_sample.target_id = "google-dns";
    old_sample.status = "ok";
    const std::vector<ConnectivitySample> old_samples{old_sample};
    repo.record_samples(old_samples, now - std::chrono::hours{48});

    nexus::notify::NotificationCenter notifications;
    ScriptedProbe probe;
    Prober prober(std::make_unique<ConnectivityRepository>(db), notifications, probe,
                  /*outage_after=*/2, std::chrono::hours{24}, &reachable_gateway);

    // prober.cpp's kPruneEveryTicks is 240 and not exposed - tick comfortably past it.
    for (int i = 0; i < 241; ++i) {
        prober.tick();
    }

    const auto samples = repo.samples_since("google-dns", now - std::chrono::hours{72});
    REQUIRE_FALSE(samples.empty()); // the prober's own recent ticks remain
    REQUIRE(std::none_of(samples.begin(), samples.end(), [&](const auto& point) {
        return point.at < now - std::chrono::hours{24};
    }));
}
