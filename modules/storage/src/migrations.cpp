#include "nexus/module/storage/storage_repository.hpp"

#include <array>

#include "nexus/db/migration.hpp"

namespace nexus::module::storage {

namespace {

// Spec section 7, Storage tables.
constexpr std::string_view kSchemaUp = R"sql(
CREATE TABLE file_scans (
    id                TEXT PRIMARY KEY,
    root              TEXT NOT NULL,
    started_at        TEXT NOT NULL,
    finished_at       TEXT,
    files_seen        INTEGER NOT NULL DEFAULT 0,
    bytes_seen        INTEGER NOT NULL DEFAULT 0,
    duplicate_groups  INTEGER NOT NULL DEFAULT 0,
    reclaimable_bytes INTEGER NOT NULL DEFAULT 0,
    state             TEXT NOT NULL DEFAULT 'running'
);

CREATE TABLE duplicate_groups (
    id                INTEGER PRIMARY KEY AUTOINCREMENT,
    scan_id           TEXT NOT NULL REFERENCES file_scans(id) ON DELETE CASCADE,
    digest            TEXT NOT NULL,
    file_size         INTEGER NOT NULL,
    file_count        INTEGER NOT NULL,
    reclaimable_bytes INTEGER NOT NULL
);
CREATE INDEX idx_duplicate_groups_scan ON duplicate_groups(scan_id);

CREATE TABLE scanned_files (
    id        INTEGER PRIMARY KEY AUTOINCREMENT,
    scan_id   TEXT NOT NULL REFERENCES file_scans(id) ON DELETE CASCADE,
    path      TEXT NOT NULL,
    size      INTEGER NOT NULL,
    mtime     TEXT,
    digest    TEXT,
    group_id  INTEGER REFERENCES duplicate_groups(id) ON DELETE SET NULL
);
CREATE INDEX idx_scanned_files_scan ON scanned_files(scan_id);
CREATE INDEX idx_scanned_files_group ON scanned_files(group_id);
)sql";

constexpr std::array<nexus::db::Migration, 1> kMigrations{{
    {1, "storage_schema", kSchemaUp},
}};

} // namespace

std::span<const nexus::db::Migration> storage_migrations() {
    return kMigrations;
}

} // namespace nexus::module::storage
