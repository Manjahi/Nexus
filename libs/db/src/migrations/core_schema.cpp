#include "nexus/db/migration.hpp"

#include <array>

namespace nexus::db {

namespace {

// Shared suite metadata. Module-specific tables (storage, network, health,
// backup, search, ...) are added by their own migrations as those modules land.
constexpr std::string_view kCoreSchemaUp = R"sql(
CREATE TABLE app_settings (
    key         TEXT PRIMARY KEY,
    value       TEXT NOT NULL,
    updated_at  TEXT NOT NULL
);

CREATE TABLE machines (
    id          TEXT PRIMARY KEY,
    hostname    TEXT NOT NULL,
    os          TEXT NOT NULL,
    first_seen  TEXT NOT NULL,
    last_seen   TEXT NOT NULL
);

CREATE TABLE jobs (
    id          TEXT PRIMARY KEY,
    module      TEXT NOT NULL,
    kind        TEXT NOT NULL,
    schedule    TEXT,
    enabled     INTEGER NOT NULL DEFAULT 1,
    config      TEXT,
    created_at  TEXT NOT NULL,
    updated_at  TEXT NOT NULL
);

CREATE TABLE job_runs (
    id          TEXT PRIMARY KEY,
    job_id      TEXT NOT NULL REFERENCES jobs(id) ON DELETE CASCADE,
    state       TEXT NOT NULL,
    started_at  TEXT,
    finished_at TEXT,
    progress    REAL NOT NULL DEFAULT 0,
    message     TEXT,
    error       TEXT
);
CREATE INDEX idx_job_runs_job_id ON job_runs(job_id);
CREATE INDEX idx_job_runs_state ON job_runs(state);

CREATE TABLE notifications (
    id          TEXT PRIMARY KEY,
    module      TEXT,
    severity    TEXT NOT NULL,
    title       TEXT NOT NULL,
    body        TEXT,
    created_at  TEXT NOT NULL,
    read_at     TEXT
);
CREATE INDEX idx_notifications_unread ON notifications(read_at) WHERE read_at IS NULL;

CREATE TABLE audit_logs (
    id          TEXT PRIMARY KEY,
    actor       TEXT,
    action      TEXT NOT NULL,
    target      TEXT,
    detail      TEXT,
    created_at  TEXT NOT NULL
);
CREATE INDEX idx_audit_logs_created_at ON audit_logs(created_at);

CREATE TABLE reports (
    id          TEXT PRIMARY KEY,
    module      TEXT,
    kind        TEXT NOT NULL,
    title       TEXT NOT NULL,
    format      TEXT NOT NULL,
    path        TEXT,
    created_at  TEXT NOT NULL
);
CREATE INDEX idx_reports_created_at ON reports(created_at);
)sql";

constexpr std::array<Migration, 1> kMigrations{{
    {1, "core_schema", kCoreSchemaUp},
}};

} // namespace

std::span<const Migration> core_migrations() {
    return kMigrations;
}

} // namespace nexus::db
