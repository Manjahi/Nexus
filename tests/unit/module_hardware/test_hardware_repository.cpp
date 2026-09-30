#include "nexus/db/database.hpp"
#include "nexus/db/migration.hpp"
#include "nexus/module/hardware/hardware_repository.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <vector>

using namespace nexus::module::hardware;
using nexus::core::now;

namespace {
nexus::db::Database migrated_db() {
    auto db = nexus::db::Database::open_in_memory();
    nexus::db::migrate(db, "hardware", hardware_migrations());
    return db;
}
} // namespace

TEST_CASE("migration seeds default thresholds", "[hardware][repo]") {
    auto db = migrated_db();
    HardwareRepository repo(db);

    const auto all = repo.thresholds();
    REQUIRE(all.size() == 2);
    REQUIRE(all.front().metric == "cpu.total");
    REQUIRE(all.front().comparison == Comparison::GreaterThan);
    REQUIRE(all.front().breached_by(0.99));
    REQUIRE_FALSE(all.front().breached_by(0.10));
}

TEST_CASE("metric samples round-trip as a series", "[hardware][repo]") {
    auto db = migrated_db();
    HardwareRepository repo(db);
    const auto t0 = now();

    const std::array<MetricSample, 3> batch{{
        {"cpu.total", "", 0.25},
        {"cpu.core", "0", 0.40},
        {"cpu.core", "1", 0.10},
    }};
    repo.record_metrics(batch, t0);
    repo.record_metrics(std::array<MetricSample, 1>{{{"cpu.total", "", 0.35}}},
                        t0 + std::chrono::seconds{1});

    const auto series = repo.metric_series("cpu.total", "", t0 - std::chrono::seconds{5});
    REQUIRE(series.size() == 2);
    REQUIRE(series[0].value == 0.25);
    REQUIRE(series[1].value == 0.35);

    REQUIRE(repo.metric_series("cpu.core", "0", t0 - std::chrono::seconds{5}).size() == 1);
    REQUIRE(repo.metric_series("cpu.total", "", t0 + std::chrono::seconds{30}).empty());
}

TEST_CASE("latest_snapshot returns the newest value per metric+scope", "[hardware][repo]") {
    auto db = migrated_db();
    HardwareRepository repo(db);
    const auto t0 = now();

    repo.record_metrics(
        std::array<MetricSample, 2>{{{"cpu.total", "", 0.2}, {"cpu.core", "0", 0.3}}}, t0);
    repo.record_metrics(
        std::array<MetricSample, 2>{{{"cpu.total", "", 0.6}, {"cpu.core", "0", 0.7}}},
        t0 + std::chrono::seconds{3});

    const auto snap = repo.latest_snapshot();
    REQUIRE(snap.size() == 2);
    for (const auto& m : snap) {
        if (m.metric == "cpu.total") {
            REQUIRE(m.value == 0.6);
        }
    }
}

TEST_CASE("latest_processes returns the most recent snapshot, cpu-sorted", "[hardware][repo]") {
    auto db = migrated_db();
    HardwareRepository repo(db);
    const auto t0 = now();

    repo.record_processes(std::array<ProcessSample, 1>{{{1, "old.exe", 0.9, 100}}}, t0);
    const std::array<ProcessSample, 3> snap{{
        {10, "a.exe", 0.1, 500},
        {11, "b.exe", 0.7, 400},
        {12, "c.exe", 0.3, 300},
    }};
    repo.record_processes(snap, t0 + std::chrono::seconds{1});

    const auto latest = repo.latest_processes(10);
    REQUIRE(latest.size() == 3);
    REQUIRE(latest[0].name == "b.exe");
    REQUIRE(latest[2].name == "a.exe");
}

TEST_CASE("threshold upsert / delete", "[hardware][repo]") {
    auto db = migrated_db();
    HardwareRepository repo(db);

    repo.upsert_threshold(
        {"disk-low", "disk.free_fraction", "C:\\", Comparison::LessThan, 0.1, "error", true});
    auto all = repo.thresholds();
    REQUIRE(all.size() == 3);

    repo.upsert_threshold(
        {"disk-low", "disk.free_fraction", "C:\\", Comparison::LessThan, 0.05, "error", false});
    REQUIRE(repo.thresholds(/*enabled_only=*/true).size() == 2);

    REQUIRE(repo.delete_threshold("disk-low"));
    REQUIRE_FALSE(repo.delete_threshold("disk-low"));
    REQUIRE(repo.thresholds().size() == 2);
}

TEST_CASE("prune_before drops old samples only", "[hardware][repo]") {
    auto db = migrated_db();
    HardwareRepository repo(db);
    const auto t0 = now();

    repo.record_metrics(std::array<MetricSample, 1>{{{"cpu.total", "", 0.5}}},
                        t0 - std::chrono::hours{48});
    repo.record_metrics(std::array<MetricSample, 1>{{{"cpu.total", "", 0.6}}}, t0);
    repo.record_processes(std::array<ProcessSample, 1>{{{1, "x", 0.1, 1}}},
                          t0 - std::chrono::hours{48});

    const auto removed = repo.prune_before(t0 - std::chrono::hours{24});
    REQUIRE(removed == 2);
    REQUIRE(repo.metric_series("cpu.total", "", t0 - std::chrono::hours{72}).size() == 1);
}
