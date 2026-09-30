#include "nexus/db/migration.hpp"
#include "nexus/module/backup/backup_repository.hpp"

#include <array>

namespace nexus::module::backup {

namespace {

// Spec section 7, Backup tables.
constexpr std::string_view kSchemaUp = R"sql(
CREATE TABLE backup_jobs (
    id             TEXT PRIMARY KEY,
    name           TEXT NOT NULL DEFAULT '',
    source_root    TEXT NOT NULL,
    destination    TEXT NOT NULL,
    schedule       TEXT,
    retention_keep INTEGER NOT NULL DEFAULT 10,
    enabled        INTEGER NOT NULL DEFAULT 1,
    exclusions     TEXT NOT NULL DEFAULT '',
    created_at     TEXT NOT NULL
);

CREATE TABLE snapshots (
    id            TEXT PRIMARY KEY,
    backup_job_id TEXT NOT NULL REFERENCES backup_jobs(id) ON DELETE CASCADE,
    started_at    TEXT NOT NULL,
    finished_at   TEXT,
    state         TEXT NOT NULL DEFAULT 'running',
    file_count    INTEGER NOT NULL DEFAULT 0,
    total_bytes   INTEGER NOT NULL DEFAULT 0,
    new_bytes     INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX idx_snapshots_job ON snapshots(backup_job_id, started_at);

CREATE TABLE snapshot_files (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    snapshot_id TEXT NOT NULL REFERENCES snapshots(id) ON DELETE CASCADE,
    path        TEXT NOT NULL,
    size        INTEGER NOT NULL,
    mtime       TEXT,
    digest      TEXT NOT NULL
);
CREATE INDEX idx_snapshot_files_snapshot ON snapshot_files(snapshot_id);

CREATE TABLE restore_jobs (
    id             TEXT PRIMARY KEY,
    snapshot_id    TEXT NOT NULL,
    target_dir     TEXT NOT NULL,
    started_at     TEXT NOT NULL,
    finished_at    TEXT,
    state          TEXT NOT NULL,
    files_restored INTEGER NOT NULL DEFAULT 0
);
)sql";

// Verify (BackupEngine::verify()) previously only ever reported its result
// transiently in the desktop UI's status label - nothing persisted whether
// a snapshot had ever actually been checked. The Continuity module's
// readiness scoring needs that as a real signal (an unverified backup isn't
// proven trustworthy), so it's now recorded here instead of invented.
constexpr std::string_view kAddVerifiedAt = R"sql(
ALTER TABLE snapshots ADD COLUMN verified_at TEXT;
)sql";

// C1: one-way sync (Mirror mode) alongside the existing content-addressed
// Snapshot mode. A Mirror job has no snapshot history to show - just the
// state of its last sync - so that state lives directly on the job row
// rather than in a new table.
constexpr std::string_view kAddSyncMode = R"sql(
ALTER TABLE backup_jobs ADD COLUMN mode TEXT NOT NULL DEFAULT 'snapshot';
ALTER TABLE backup_jobs ADD COLUMN last_synced_at TEXT;
ALTER TABLE backup_jobs ADD COLUMN last_sync_files INTEGER NOT NULL DEFAULT 0;
ALTER TABLE backup_jobs ADD COLUMN last_sync_deleted INTEGER NOT NULL DEFAULT 0;
ALTER TABLE backup_jobs ADD COLUMN last_sync_bytes INTEGER NOT NULL DEFAULT 0;
)sql";

constexpr std::array<nexus::db::Migration, 3> kMigrations{{
    {1, "backup_schema", kSchemaUp},
    {2, "backup_verified_at", kAddVerifiedAt},
    {3, "backup_sync_mode", kAddSyncMode},
}};

} // namespace

std::span<const nexus::db::Migration> backup_migrations() {
    return kMigrations;
}

} // namespace nexus::module::backup
