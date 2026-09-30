#include "nexus/db/database.hpp"
#include "nexus/db/migration.hpp"
#include "nexus/module/connectivity/connectivity_repository.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <optional>
#include <span>
#include <utility>

using namespace nexus::module::connectivity;
using nexus::core::now;

namespace {
nexus::db::Database migrated_db() {
    auto db = nexus::db::Database::open_in_memory();
    nexus::db::migrate(db, "connectivity", connectivity_migrations());
    return db;
}

ConnectivitySample ok_sample(std::string target, std::chrono::microseconds rtt) {
    return {std::move(target), "ok", rtt, ""};
}
ConnectivitySample bad_sample(std::string target) {
    return {std::move(target), "timeout", std::nullopt, "no reply"};
}
} // namespace

TEST_CASE("migration seeds probe targets", "[connectivity][repo]") {
    auto db = migrated_db();
    ConnectivityRepository repo(db);

    const auto targets = repo.targets();
    REQUIRE(targets.size() == 4);
    const bool has_http = std::any_of(targets.begin(), targets.end(), [](const ProbeTarget& t) {
        return t.kind == ProbeKind::Http;
    });
    REQUIRE(has_http);
    const bool has_dns = std::any_of(targets.begin(), targets.end(),
                                     [](const ProbeTarget& t) { return t.kind == ProbeKind::Dns; });
    REQUIRE(has_dns);
}

TEST_CASE("target upsert / delete and enabled filter", "[connectivity][repo]") {
    auto db = migrated_db();
    ConnectivityRepository repo(db);

    repo.upsert_target(
        {"router", ProbeKind::Tcp, "192.168.1.1", std::uint16_t{443}, "Router", true});
    REQUIRE(repo.targets().size() == 5);

    repo.upsert_target(
        {"router", ProbeKind::Tcp, "192.168.1.1", std::uint16_t{443}, "Router", false});
    REQUIRE(repo.targets(/*enabled_only=*/true).size() == 4);

    REQUIRE(repo.delete_target("router"));
    REQUIRE_FALSE(repo.delete_target("router"));
    REQUIRE(repo.targets().size() == 4);
}

// UFR-010-adjacent housekeeping: connectivity's schema-version-2 migration
// (widening probe_targets.kind's CHECK constraint to allow 'dns') is the
// first multi-version migration anywhere in this codebase - it rebuilds the
// table (SQLite can't ALTER a CHECK constraint), so this specifically
// verifies existing rows survive that rebuild rather than trusting migrate()
// blindly.
TEST_CASE("the v1->v2 probe_targets rebuild preserves existing rows and adds dns support",
          "[connectivity][repo][migration]") {
    auto db = nexus::db::Database::open_in_memory();
    // Apply only v1 first, seed a custom target under the old (icmp/tcp/http
    // only) schema, then apply the rest (v2) and confirm it survived.
    const auto all_migrations = connectivity_migrations();
    REQUIRE(all_migrations.size() >= 2);
    nexus::db::migrate(db, "connectivity", all_migrations.subspan(0, 1));

    {
        ConnectivityRepository repo(db);
        repo.upsert_target(
            {"custom", ProbeKind::Tcp, "10.0.0.1", std::uint16_t{22}, "Custom", true});
    }
    REQUIRE(nexus::db::schema_version(db, "connectivity") == 1);

    nexus::db::migrate(db, "connectivity", all_migrations);
    // Not hardcoded as a literal version number - a later migration (e.g.
    // Phase 6's connectivity_path_status table) legitimately moves this
    // forward; this test only cares that migrating all the way lands on
    // the newest version, not any specific one.
    REQUIRE(nexus::db::schema_version(db, "connectivity") == all_migrations.back().version);

    ConnectivityRepository repo(db);
    const auto targets = repo.targets();
    const auto custom = std::find_if(targets.begin(), targets.end(),
                                     [](const ProbeTarget& t) { return t.id == "custom"; });
    REQUIRE(custom != targets.end());
    REQUIRE(custom->kind == ProbeKind::Tcp);
    REQUIRE(custom->address == "10.0.0.1");
    REQUIRE(custom->port == std::uint16_t{22});

    // The new default DNS target from v2's seed data is present too.
    const auto dns_check = std::find_if(targets.begin(), targets.end(),
                                        [](const ProbeTarget& t) { return t.id == "dns-check"; });
    REQUIRE(dns_check != targets.end());
    REQUIRE(dns_check->kind == ProbeKind::Dns);

    // And the widened CHECK constraint actually accepts 'dns' now (this
    // would throw if it still didn't).
    repo.upsert_target({"another-dns", ProbeKind::Dns, "example.com", std::nullopt, "", true});
    REQUIRE(repo.targets().size() == targets.size() + 1);
}

TEST_CASE("samples and uptime fraction", "[connectivity][repo]") {
    auto db = migrated_db();
    ConnectivityRepository repo(db);
    const auto t0 = now();

    const std::array<ConnectivitySample, 2> first{{
        ok_sample("google-dns", std::chrono::microseconds{12000}),
        bad_sample("cloudflare-dns"),
    }};
    repo.record_samples(first, t0);
    repo.record_samples(std::array<ConnectivitySample, 1>{{ok_sample(
                            "google-dns", std::chrono::microseconds{9000})}},
                        t0 + std::chrono::seconds{15});

    const auto series = repo.samples_since("google-dns", t0 - std::chrono::minutes{1});
    REQUIRE(series.size() == 2);
    REQUIRE(series[0].rtt.has_value());
    REQUIRE(series[0].rtt->count() == 12000);

    REQUIRE(repo.uptime_fraction("google-dns", t0 - std::chrono::minutes{1}) == 1.0);
    REQUIRE(repo.uptime_fraction("cloudflare-dns", t0 - std::chrono::minutes{1}) == 0.0);
    REQUIRE_FALSE(repo.uptime_fraction("nobody", t0 - std::chrono::minutes{1}).has_value());
}

TEST_CASE("reliability_stats computes packet loss and jitter via nexus::net::summarize",
          "[connectivity][repo]") {
    auto db = migrated_db();
    ConnectivityRepository repo(db);
    const auto t0 = now();

    // 4 ok samples (RTTs 10/12/9/13ms) and 1 failure (5 total, 20% loss).
    repo.record_samples(std::array<ConnectivitySample, 1>{{ok_sample(
                            "google-dns", std::chrono::microseconds{10000})}},
                        t0);
    repo.record_samples(std::array<ConnectivitySample, 1>{{ok_sample(
                            "google-dns", std::chrono::microseconds{12000})}},
                        t0 + std::chrono::seconds{1});
    repo.record_samples(std::array<ConnectivitySample, 1>{{bad_sample("google-dns")}},
                        t0 + std::chrono::seconds{2});
    repo.record_samples(std::array<ConnectivitySample, 1>{{ok_sample(
                            "google-dns", std::chrono::microseconds{9000})}},
                        t0 + std::chrono::seconds{3});
    repo.record_samples(std::array<ConnectivitySample, 1>{{ok_sample(
                            "google-dns", std::chrono::microseconds{13000})}},
                        t0 + std::chrono::seconds{4});

    const auto stats = repo.reliability_stats("google-dns", t0 - std::chrono::minutes{1});
    REQUIRE(stats.sent == 5);
    REQUIRE(stats.received == 4);
    REQUIRE(stats.loss_fraction == Catch::Approx(0.2));
    // summarize() computes jitter across all *received* samples in order
    // (a loss in between doesn't reset it - see test_latency_stats.cpp):
    // deltas |12000-10000|, |9000-12000|, |13000-9000| = 2000/3000/4000,
    // mean = 3000us.
    REQUIRE(stats.jitter.count() == 3000);
}

TEST_CASE("reliability_stats on a target with no samples reports zero/empty",
          "[connectivity][repo]") {
    auto db = migrated_db();
    ConnectivityRepository repo(db);

    const auto stats = repo.reliability_stats("nobody", now() - std::chrono::minutes{1});
    REQUIRE(stats.sent == 0);
    REQUIRE(stats.received == 0);
    REQUIRE(stats.loss_fraction == 0.0);
    REQUIRE(stats.jitter == std::chrono::microseconds{0});
}

TEST_CASE("speed test round-trips through the repository", "[connectivity][repo][speedtest]") {
    auto db = migrated_db();
    ConnectivityRepository repo(db);
    const auto t0 = now();

    SpeedTestRecord successful;
    successful.ran_at = t0;
    successful.download_bps = 123456.0;
    successful.server = "https://example.test/download";
    repo.record_speed_test(successful);

    SpeedTestRecord failed;
    failed.ran_at = t0 + std::chrono::hours{1};
    failed.server = "https://example.test/download"; // download_bps left nullopt (probe failed)
    repo.record_speed_test(failed);

    const auto recent = repo.recent_speed_tests();
    REQUIRE(recent.size() == 2);
    // Most recent first.
    REQUIRE_FALSE(recent[0].download_bps.has_value());
    REQUIRE(recent[0].server == "https://example.test/download");
    REQUIRE(recent[1].download_bps.has_value());
    REQUIRE(recent[1].download_bps.value() == Catch::Approx(123456.0));
}

TEST_CASE("recent_speed_tests respects the limit", "[connectivity][repo][speedtest]") {
    auto db = migrated_db();
    ConnectivityRepository repo(db);
    const auto t0 = now();

    for (int i = 0; i < 5; ++i) {
        SpeedTestRecord record;
        record.ran_at = t0 + std::chrono::minutes{i};
        record.download_bps = 1000.0 * i;
        repo.record_speed_test(record);
    }

    REQUIRE(repo.recent_speed_tests(50).size() == 5);
    REQUIRE(repo.recent_speed_tests(2).size() == 2);
}

TEST_CASE("outage lifecycle", "[connectivity][repo]") {
    auto db = migrated_db();
    ConnectivityRepository repo(db);
    const auto t0 = now();

    REQUIRE_FALSE(repo.open_outage("google-dns").has_value());

    const auto id = repo.begin_outage("google-dns", t0);
    REQUIRE(repo.open_outage("google-dns") == id);
    repo.bump_outage(id);
    repo.bump_outage(id);

    repo.end_outage(id, t0 + std::chrono::seconds{45});
    REQUIRE_FALSE(repo.open_outage("google-dns").has_value());

    const auto outages = repo.recent_outages();
    REQUIRE(outages.size() == 1);
    REQUIRE(outages[0].samples_failed == 3);
    REQUIRE(outages[0].ended_at.has_value());
}

// Spec section 9 hook #4 (PC<->router<->internet path visualization):
// Prober persists its total-outage verdict here instead of only posting a
// notification, so the UI can render current state without parsing
// notification text.
TEST_CASE("path status has no record until one is written, then is overwritten in place",
          "[connectivity][repo][path_status]") {
    auto db = migrated_db();
    ConnectivityRepository repo(db);

    REQUIRE_FALSE(repo.latest_path_status().has_value());

    const auto t0 = now();
    repo.record_path_status(PathStatus::LocalIssue, t0);
    {
        const auto status = repo.latest_path_status();
        REQUIRE(status.has_value());
        REQUIRE(status->status == PathStatus::LocalIssue);
    }

    // A single row that's overwritten, not a history - the point is "what's
    // the current verdict," not a log of past ones (outages/samples already
    // cover history).
    const auto t1 = t0 + std::chrono::minutes{1};
    repo.record_path_status(PathStatus::AllOk, t1);
    const auto status = repo.latest_path_status();
    REQUIRE(status.has_value());
    REQUIRE(status->status == PathStatus::AllOk);
}

TEST_CASE("path_status string round-trip covers every case", "[connectivity][repo][path_status]") {
    for (const auto status : {PathStatus::Unknown, PathStatus::AllOk, PathStatus::LocalIssue,
                              PathStatus::BeyondRouter, PathStatus::NoGatewayFound}) {
        REQUIRE(path_status_from_string(to_string(status)) == status);
    }
    REQUIRE(path_status_from_string("garbage") == PathStatus::Unknown);
}
