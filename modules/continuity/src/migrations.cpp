#include "nexus/db/migration.hpp"
#include "nexus/module/continuity/continuity_repository.hpp"

#include <array>

namespace nexus::module::continuity {

namespace {

constexpr std::string_view kSchemaUp = R"sql(
CREATE TABLE continuity_assets (
    id              TEXT PRIMARY KEY,
    label           TEXT NOT NULL,
    kind            TEXT NOT NULL CHECK (kind IN ('file', 'folder', 'app', 'credential')),
    path            TEXT NOT NULL DEFAULT '',
    vault_entry_id  TEXT NOT NULL DEFAULT '',
    criticality     TEXT NOT NULL DEFAULT 'medium' CHECK (criticality IN ('low', 'medium', 'high')),
    created_at      TEXT NOT NULL
);

CREATE TABLE continuity_rehearsals (
    id              TEXT PRIMARY KEY,
    scenario        TEXT NOT NULL,
    started_at      TEXT NOT NULL,
    finished_at     TEXT,
    outcome         TEXT NOT NULL DEFAULT 'running',
    files_restored  INTEGER NOT NULL DEFAULT 0,
    bytes_restored  INTEGER NOT NULL DEFAULT 0,
    detail          TEXT NOT NULL DEFAULT ''
);
CREATE INDEX idx_continuity_rehearsals_time ON continuity_rehearsals(started_at);

-- Single-row table: the Recovery Capsule has exactly one "last export" time.
CREATE TABLE continuity_capsule (
    id              INTEGER PRIMARY KEY CHECK (id = 1),
    exported_at     TEXT NOT NULL
);
)sql";

constexpr std::array<nexus::db::Migration, 1> kMigrations{{
    {1, "continuity_schema", kSchemaUp},
}};

} // namespace

std::span<const nexus::db::Migration> continuity_migrations() {
    return kMigrations;
}

} // namespace nexus::module::continuity
