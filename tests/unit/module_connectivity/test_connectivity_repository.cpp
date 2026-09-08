#include "nexus/module/connectivity/connectivity_repository.hpp"

#include "nexus/db/database.hpp"
#include "nexus/db/migration.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
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
    REQUIRE(targets.size() == 3);
    const bool has_http =
        std::any_of(targets.begin(), targets.end(),
                    [](const ProbeTarget& t) { return t.kind == ProbeKind::Http; });
    REQUIRE(has_http);
}

TEST_CASE("target upsert / delete and enabled filter", "[connectivity][repo]") {
    auto db = migrated_db();
    ConnectivityRepository repo(db);

    repo.upsert_target({"router", ProbeKind::Tcp, "192.168.1.1", std::uint16_t{443}, "Router", true});
    REQUIRE(repo.targets().size() == 4);

    repo.upsert_target({"router", ProbeKind::Tcp, "192.168.1.1", std::uint16_t{443}, "Router", false});
    REQUIRE(repo.targets(/*enabled_only=*/true).size() == 3);

    REQUIRE(repo.delete_target("router"));
    REQUIRE_FALSE(repo.delete_target("router"));
    REQUIRE(repo.targets().size() == 3);
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
    repo.record_samples(std::array<ConnectivitySample, 1>{{ok_sample("google-dns", std::chrono::microseconds{9000})}},
                        t0 + std::chrono::seconds{15});

    const auto series = repo.samples_since("google-dns", t0 - std::chrono::minutes{1});
    REQUIRE(series.size() == 2);
    REQUIRE(series[0].rtt.has_value());
    REQUIRE(series[0].rtt->count() == 12000);

    REQUIRE(repo.uptime_fraction("google-dns", t0 - std::chrono::minutes{1}) == 1.0);
    REQUIRE(repo.uptime_fraction("cloudflare-dns", t0 - std::chrono::minutes{1}) == 0.0);
    REQUIRE_FALSE(repo.uptime_fraction("nobody", t0 - std::chrono::minutes{1}).has_value());
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
