#include "nexus/module/connectivity/prober.hpp"

#include "nexus/db/database.hpp"
#include "nexus/db/migration.hpp"
#include "nexus/module/connectivity/connectivity_repository.hpp"
#include "nexus/notify/notification_center.hpp"

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
                  /*outage_after=*/2, std::chrono::hours{24});

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
