#include "nexus/module/continuity/continuity_scenarios.hpp"
#include "nexus/module/continuity/continuity_repository.hpp"

#include "nexus/module/backup/backup_repository.hpp"

#include "nexus/db/database.hpp"
#include "nexus/db/migration.hpp"
#include "nexus/db/statement.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <stdexcept>

using namespace nexus::module::continuity;
using nexus::module::backup::BackupJob;
using nexus::module::backup::BackupRepository;

namespace {
nexus::db::Database migrated_db() {
    auto db = nexus::db::Database::open_in_memory();
    nexus::db::migrate(db, "continuity", continuity_migrations());
    nexus::db::migrate(db, "backup", nexus::module::backup::backup_migrations());
    return db;
}

const ScenarioStatus& find(const std::vector<ScenarioStatus>& scenarios, ScenarioKind kind) {
    for (const auto& s : scenarios) {
        if (s.kind == kind) {
            return s;
        }
    }
    throw std::runtime_error("scenario not found");
}

// begin_snapshot() always timestamps at now() - to test "does retention span
// enough history", a second snapshot needs a controllable, backdated
// started_at, which the repository API deliberately doesn't expose (no
// production caller should ever backdate a real snapshot). Inserted directly
// instead of adding a test-only parameter to the real API.
void insert_backdated_snapshot(nexus::db::Database& db, const nexus::core::Uuid& job_id,
                               nexus::core::Timestamp started_at) {
    nexus::db::Statement stmt = db.prepare(
        "INSERT INTO snapshots (id, backup_job_id, started_at, state) VALUES (?, ?, ?, 'completed')");
    stmt.bind(1, nexus::core::Uuid::generate().to_string());
    stmt.bind(2, job_id.to_string());
    stmt.bind(3, nexus::core::to_iso8601(started_at));
    stmt.step();
}
} // namespace

TEST_CASE("scenario_name covers every kind", "[continuity][scenarios]") {
    CHECK(scenario_name(ScenarioKind::DiskFailure) == "Disk Failure");
    CHECK(scenario_name(ScenarioKind::ComputerTheft) == "Computer Theft");
    CHECK(scenario_name(ScenarioKind::RansomwareEvent) == "Ransomware Event");
    CHECK(scenario_name(ScenarioKind::NewPcMigration) == "New-PC Migration");
}

TEST_CASE("with nothing configured, every scenario is unready with real gaps",
         "[continuity][scenarios]") {
    auto db = migrated_db();
    const auto scenarios = evaluate_scenarios(db);
    REQUIRE(scenarios.size() == 4);
    for (const auto& s : scenarios) {
        CHECK_FALSE(s.ready);
        bool any_unmet = false;
        for (const auto& check : s.checks) {
            any_unmet = any_unmet || !check.passed;
        }
        CHECK(any_unmet);
    }
}

TEST_CASE("Disk Failure becomes ready once a job has a verified latest snapshot",
         "[continuity][scenarios]") {
    auto db = migrated_db();
    BackupRepository backup(db);

    BackupJob job;
    job.source_root = "C:/data";
    job.destination = "D:/backups";
    const auto job_id = backup.upsert_job(job);

    CHECK_FALSE(find(evaluate_scenarios(db), ScenarioKind::DiskFailure).ready);

    const auto snap_id = backup.begin_snapshot(job_id);
    backup.finish_snapshot(snap_id, "completed", 1, 100, 100);
    CHECK_FALSE(find(evaluate_scenarios(db), ScenarioKind::DiskFailure).ready); // not verified yet

    backup.mark_verified(snap_id, nexus::core::now());
    CHECK(find(evaluate_scenarios(db), ScenarioKind::DiskFailure).ready);
}

TEST_CASE("Computer Theft and New-PC Migration need a capsule export and full coverage",
         "[continuity][scenarios]") {
    auto db = migrated_db();
    BackupRepository backup(db);
    ContinuityRepository continuity(db);

    BackupJob job;
    job.source_root = "C:/data";
    job.destination = "D:/backups";
    const auto job_id = backup.upsert_job(job);
    const auto snap_id = backup.begin_snapshot(job_id);
    backup.finish_snapshot(snap_id, "completed", 1, 100, 100);

    TrackedAsset asset;
    asset.label = "Covered file";
    asset.kind = AssetKind::File;
    asset.path = "C:/data/file.txt";
    continuity.upsert_asset(asset);

    // Covered, but no capsule export yet.
    CHECK_FALSE(find(evaluate_scenarios(db), ScenarioKind::ComputerTheft).ready);
    CHECK_FALSE(find(evaluate_scenarios(db), ScenarioKind::NewPcMigration).ready);

    continuity.record_capsule_export(nexus::core::now());
    CHECK(find(evaluate_scenarios(db), ScenarioKind::ComputerTheft).ready);
    CHECK(find(evaluate_scenarios(db), ScenarioKind::NewPcMigration).ready);
}

TEST_CASE("Ransomware Event needs retained history spanning at least a day",
         "[continuity][scenarios]") {
    auto db = migrated_db();
    BackupRepository backup(db);

    BackupJob job;
    job.source_root = "C:/data";
    job.destination = "D:/backups";
    job.retention_keep = 10;
    const auto job_id = backup.upsert_job(job);

    const auto snap_id = backup.begin_snapshot(job_id);
    backup.finish_snapshot(snap_id, "completed", 1, 100, 100);
    CHECK_FALSE(find(evaluate_scenarios(db), ScenarioKind::RansomwareEvent).ready); // only one

    // A second snapshot only two hours before "now" isn't enough history yet.
    insert_backdated_snapshot(db, job_id, nexus::core::now() - std::chrono::hours{2});
    CHECK_FALSE(find(evaluate_scenarios(db), ScenarioKind::RansomwareEvent).ready);

    // But one two days back is.
    insert_backdated_snapshot(db, job_id, nexus::core::now() - std::chrono::hours{48});
    CHECK(find(evaluate_scenarios(db), ScenarioKind::RansomwareEvent).ready);
}
