#include "nexus/module/connectivity/prober.hpp"

#include "nexus/db/database.hpp"
#include "nexus/db/migration.hpp"
#include "nexus/module/connectivity/connectivity_repository.hpp"
#include "nexus/notify/notification_center.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>

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

} // namespace

TEST_CASE("prober records one sample per enabled target each tick", "[connectivity][prober]") {
    auto db = migrated_db();
    nexus::notify::NotificationCenter notifications;
    ScriptedProbe probe;

    Prober prober(std::make_unique<ConnectivityRepository>(db), notifications, probe);
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
                  /*outage_after=*/2);
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
    Prober prober(std::make_unique<ConnectivityRepository>(db), notifications, ScriptedProbe{});

    prober.set_active(false);
    prober.tick();

    ConnectivityRepository repo(db);
    REQUIRE(repo.samples_since("google-dns", nexus::core::now() - std::chrono::minutes{1}).empty());
}
