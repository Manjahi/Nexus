#pragma once

#include "nexus/core/id.hpp"
#include "nexus/core/time.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace nexus::db {
class Database;
}

namespace nexus::services {

struct AuditEntry {
    nexus::core::Uuid id;
    std::string actor;
    std::string action;
    std::string target;
    std::string detail;
    nexus::core::Timestamp created_at{};
};

/// Append-only trail for destructive and administrative actions (UFR-007),
/// backed by the `audit_logs` table.
class AuditLog {
public:
    explicit AuditLog(nexus::db::Database& db) noexcept : db_(&db) {}

    /// Records an action and returns the new entry id. Empty `target`, `detail`,
    /// or `actor` are stored as NULL.
    nexus::core::Uuid record(std::string action, std::string target = {}, std::string detail = {},
                             std::string actor = {});

    /// Most recent entries first.
    [[nodiscard]] std::vector<AuditEntry> recent(std::size_t limit = 100) const;

private:
    nexus::db::Database* db_;
};

} // namespace nexus::services
