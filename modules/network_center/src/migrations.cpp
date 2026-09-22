#include "nexus/module/network_center/network_repository.hpp"

#include <array>

#include "nexus/db/migration.hpp"

namespace nexus::module::network_center {

namespace {

// Spec section 7, Network tables.
constexpr std::string_view kSchemaUp = R"sql(
CREATE TABLE networks (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    cidr       TEXT NOT NULL UNIQUE,
    label      TEXT NOT NULL DEFAULT '',
    created_at TEXT NOT NULL
);

CREATE TABLE devices (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    network_id    INTEGER NOT NULL REFERENCES networks(id) ON DELETE CASCADE,
    address       TEXT NOT NULL,
    hostname      TEXT NOT NULL DEFAULT '',
    label         TEXT NOT NULL DEFAULT '',
    status        TEXT NOT NULL DEFAULT 'unknown' CHECK (status IN ('online', 'offline', 'unknown')),
    first_seen_at TEXT NOT NULL,
    last_seen_at  TEXT NOT NULL,
    UNIQUE(network_id, address)
);
CREATE INDEX idx_devices_network ON devices(network_id);

CREATE TABLE checks (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    network_id    INTEGER NOT NULL REFERENCES networks(id) ON DELETE CASCADE,
    kind          TEXT NOT NULL CHECK (kind IN ('discovery', 'monitor')),
    started_at    TEXT NOT NULL,
    finished_at   TEXT,
    devices_found INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX idx_checks_network ON checks(network_id, started_at);

CREATE TABLE check_results (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    check_id   INTEGER NOT NULL REFERENCES checks(id) ON DELETE CASCADE,
    device_id  INTEGER NOT NULL REFERENCES devices(id) ON DELETE CASCADE,
    address    TEXT NOT NULL,
    status     TEXT NOT NULL,
    rtt_us     INTEGER,
    checked_at TEXT NOT NULL
);
CREATE INDEX idx_check_results_check ON check_results(check_id);
CREATE INDEX idx_check_results_device ON check_results(device_id, checked_at);
)sql";

// Common-service-port TCP checks (spec Phase 6): the lower-layer primitive
// (nexus::net::tcp_connect) existed and was unit-testable, but nothing ever
// called it for discovered devices. Unlike connectivity's CHECK-constraint
// widening, this is a plain column add - SQLite supports ALTER TABLE ADD
// COLUMN directly.
constexpr std::string_view kAddOpenPorts = R"sql(
ALTER TABLE devices ADD COLUMN open_ports TEXT NOT NULL DEFAULT '';
)sql";

constexpr std::array<nexus::db::Migration, 2> kMigrations{{
    {1, "network_center_schema", kSchemaUp},
    {2, "network_center_open_ports", kAddOpenPorts},
}};

} // namespace

std::span<const nexus::db::Migration> network_center_migrations() {
    return kMigrations;
}

} // namespace nexus::module::network_center
