#include "nexus/module/connectivity/connectivity_repository.hpp"

#include <array>

#include "nexus/db/migration.hpp"

namespace nexus::module::connectivity {

namespace {

// Spec section 7, Connectivity tables.
constexpr std::string_view kSchemaUp = R"sql(
CREATE TABLE probe_targets (
    id       TEXT PRIMARY KEY,
    kind     TEXT NOT NULL CHECK (kind IN ('icmp', 'tcp', 'http')),
    address  TEXT NOT NULL,
    port     INTEGER,
    label    TEXT NOT NULL DEFAULT '',
    enabled  INTEGER NOT NULL DEFAULT 1
);

CREATE TABLE connectivity_samples (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    sampled_at  TEXT NOT NULL,
    target_id   TEXT NOT NULL,
    status      TEXT NOT NULL,
    rtt_us      INTEGER,
    detail      TEXT NOT NULL DEFAULT ''
);
CREATE INDEX idx_connectivity_samples_target ON connectivity_samples(target_id, sampled_at);
CREATE INDEX idx_connectivity_samples_time ON connectivity_samples(sampled_at);

CREATE TABLE outages (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    target_id      TEXT NOT NULL,
    started_at     TEXT NOT NULL,
    ended_at       TEXT,
    samples_failed INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX idx_outages_target ON outages(target_id, started_at);

CREATE TABLE speed_tests (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    ran_at       TEXT NOT NULL,
    download_bps REAL,
    upload_bps   REAL,
    latency_us   INTEGER,
    server       TEXT NOT NULL DEFAULT ''
);

INSERT INTO probe_targets (id, kind, address, port, label) VALUES
    ('cloudflare-dns', 'icmp', '1.1.1.1', NULL, 'Cloudflare DNS'),
    ('google-dns',     'icmp', '8.8.8.8', NULL, 'Google DNS'),
    ('http-204',       'http', 'http://www.gstatic.com/generate_204', NULL, 'Captive check');
)sql";

constexpr std::array<nexus::db::Migration, 1> kMigrations{{
    {1, "connectivity_schema", kSchemaUp},
}};

} // namespace

std::span<const nexus::db::Migration> connectivity_migrations() {
    return kMigrations;
}

} // namespace nexus::module::connectivity
