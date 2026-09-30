#include "nexus/db/migration.hpp"
#include "nexus/module/hardware/hardware_repository.hpp"

#include <array>

namespace nexus::module::hardware {

namespace {

// Spec section 7, Health tables. metric_samples is a generic time series keyed
// by (metric, scope); process_samples is a periodic top-N snapshot.
constexpr std::string_view kSchemaUp = R"sql(
CREATE TABLE hw_components (
    id          TEXT PRIMARY KEY,
    kind        TEXT NOT NULL,
    name        TEXT NOT NULL,
    first_seen  TEXT NOT NULL,
    last_seen   TEXT NOT NULL
);

CREATE TABLE metric_samples (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    sampled_at  TEXT NOT NULL,
    metric      TEXT NOT NULL,
    scope       TEXT NOT NULL DEFAULT '',
    value       REAL NOT NULL
);
CREATE INDEX idx_metric_samples_lookup ON metric_samples(metric, scope, sampled_at);
CREATE INDEX idx_metric_samples_time ON metric_samples(sampled_at);

CREATE TABLE process_samples (
    id                INTEGER PRIMARY KEY AUTOINCREMENT,
    sampled_at        TEXT NOT NULL,
    pid               INTEGER NOT NULL,
    name              TEXT NOT NULL,
    cpu_fraction      REAL NOT NULL,
    working_set_bytes INTEGER NOT NULL
);
CREATE INDEX idx_process_samples_time ON process_samples(sampled_at);

CREATE TABLE thresholds (
    id          TEXT PRIMARY KEY,
    metric      TEXT NOT NULL,
    scope       TEXT NOT NULL DEFAULT '',
    comparison  TEXT NOT NULL CHECK (comparison IN ('gt', 'lt')),
    value       REAL NOT NULL,
    severity    TEXT NOT NULL,
    enabled     INTEGER NOT NULL DEFAULT 1
);

INSERT INTO thresholds (id, metric, scope, comparison, value, severity) VALUES
    ('cpu-total-high', 'cpu.total', '', 'gt', 0.95, 'warning'),
    ('mem-used-high',  'mem.used_fraction', '', 'gt', 0.92, 'warning');
)sql";

constexpr std::array<nexus::db::Migration, 1> kMigrations{{
    {1, "hardware_schema", kSchemaUp},
}};

} // namespace

std::span<const nexus::db::Migration> hardware_migrations() {
    return kMigrations;
}

} // namespace nexus::module::hardware
