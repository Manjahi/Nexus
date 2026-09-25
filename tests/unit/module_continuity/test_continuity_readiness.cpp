#include "nexus/module/continuity/continuity_readiness.hpp"
#include "nexus/module/continuity/continuity_repository.hpp"

#include "nexus/module/backup/backup_repository.hpp"

#include "nexus/db/database.hpp"
#include "nexus/db/migration.hpp"

#include <catch2/catch_test_macros.hpp>

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
} // namespace

TEST_CASE("readiness is zero-scored with no tracked assets", "[continuity][readiness]") {
    auto db = migrated_db();
    const auto report = compute_readiness(db);
    CHECK(report.tracked_count == 0);
    CHECK(report.score == 0);
    CHECK_FALSE(report.estimated_rebuild_time.has_value());
}

TEST_CASE("an asset with no covering backup job is not covered", "[continuity][readiness]") {
    auto db = migrated_db();
    ContinuityRepository continuity(db);

    TrackedAsset asset;
    asset.label = "Docs";
    asset.kind = AssetKind::Folder;
    asset.path = "C:/Users/me/Documents";
    continuity.upsert_asset(asset);

    const auto report = compute_readiness(db);
    REQUIRE(report.tracked_count == 1);
    CHECK(report.covered_count == 0);
    CHECK(report.score == 0);
    CHECK_FALSE(report.estimated_rebuild_time.has_value());
}

TEST_CASE("an asset under a backed-up job's source root is covered, then verified",
         "[continuity][readiness]") {
    auto db = migrated_db();
    ContinuityRepository continuity(db);
    BackupRepository backup(db);

    BackupJob job;
    job.source_root = "C:/Users/me/Documents";
    job.destination = "D:/backups";
    const auto job_id = backup.upsert_job(job);
    const auto snap_id = backup.begin_snapshot(job_id);
    backup.finish_snapshot(snap_id, "completed", 10, 1024 * 1024, 1024 * 1024);

    TrackedAsset asset;
    asset.label = "Docs";
    asset.kind = AssetKind::Folder;
    asset.path = "C:/Users/me/Documents/project";
    continuity.upsert_asset(asset);

    auto report = compute_readiness(db);
    REQUIRE(report.tracked_count == 1);
    CHECK(report.covered_count == 1);
    CHECK(report.verified_count == 0);
    CHECK(report.score == 70); // coverage only: round(100 * 0.7 * 1.0)
    REQUIRE(report.estimated_rebuild_time.has_value());

    backup.mark_verified(snap_id, nexus::core::now());
    report = compute_readiness(db);
    CHECK(report.verified_count == 1);
    CHECK(report.score == 100);
}

TEST_CASE("a credential asset is covered once a capsule export is recorded",
         "[continuity][readiness]") {
    auto db = migrated_db();
    ContinuityRepository continuity(db);

    TrackedAsset asset;
    asset.label = "Email password";
    asset.kind = AssetKind::Credential;
    asset.vault_entry_id = "abc123";
    continuity.upsert_asset(asset);

    auto report = compute_readiness(db);
    CHECK(report.covered_count == 0);
    CHECK(report.score == 0);

    continuity.record_capsule_export(nexus::core::now());
    report = compute_readiness(db);
    CHECK(report.covered_count == 1);
    CHECK(report.verified_count == 1);
    CHECK(report.score == 100);
}

TEST_CASE("a mixed set of covered and uncovered assets produces a proportional score",
         "[continuity][readiness]") {
    auto db = migrated_db();
    ContinuityRepository continuity(db);
    BackupRepository backup(db);

    BackupJob job;
    job.source_root = "C:/data";
    job.destination = "D:/backups";
    const auto job_id = backup.upsert_job(job);
    const auto snap_id = backup.begin_snapshot(job_id);
    backup.finish_snapshot(snap_id, "completed", 1, 100, 100);

    TrackedAsset covered;
    covered.label = "Covered";
    covered.kind = AssetKind::File;
    covered.path = "C:/data/file.txt";
    continuity.upsert_asset(covered);

    TrackedAsset uncovered;
    uncovered.label = "Uncovered";
    uncovered.kind = AssetKind::File;
    uncovered.path = "E:/elsewhere/file.txt";
    continuity.upsert_asset(uncovered);

    const auto report = compute_readiness(db);
    REQUIRE(report.tracked_count == 2);
    CHECK(report.covered_count == 1);
    CHECK(report.score == 35); // round(100 * 0.7 * 0.5)
}
