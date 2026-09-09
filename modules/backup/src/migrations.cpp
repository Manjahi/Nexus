#include "nexus/module/backup/backup_repository.hpp"

#include <array>

#include "nexus/db/migration.hpp"

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

constexpr std::array<nexus::db::Migration, 1> kMigrations{{
    {1, "backup_schema", kSchemaUp},
}};

} // namespace

std::span<const nexus::db::Migration> backup_migrations() {
    return kMigrations;
}

} // namespace nexus::module::backup
