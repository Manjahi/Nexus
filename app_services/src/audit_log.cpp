#include "nexus/services/audit_log.hpp"

#include "nexus/db/database.hpp"
#include "nexus/db/statement.hpp"

namespace nexus::services {

namespace {

void bind_optional(nexus::db::Statement& stmt, int index, const std::string& value) {
    if (value.empty()) {
        stmt.bind(index, nullptr);
    } else {
        stmt.bind(index, value);
    }
}

} // namespace

nexus::core::Uuid AuditLog::record(std::string action, std::string target, std::string detail,
                                   std::string actor) {
    const nexus::core::Uuid id = nexus::core::Uuid::generate();
    const std::string created_at = nexus::core::to_iso8601(nexus::core::now());

    nexus::db::Statement stmt = db_->prepare(
        "INSERT INTO audit_logs (id, actor, action, target, detail, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?)");
    stmt.bind(1, id.to_string());
    bind_optional(stmt, 2, actor);
    stmt.bind(3, action);
    bind_optional(stmt, 4, target);
    bind_optional(stmt, 5, detail);
    stmt.bind(6, created_at);
    stmt.step();

    return id;
}

std::vector<AuditEntry> AuditLog::recent(std::size_t limit) const {
    // rowid breaks ties for entries recorded within the same second.
    nexus::db::Statement stmt = db_->prepare(
        "SELECT id, actor, action, target, detail, created_at FROM audit_logs "
        "ORDER BY created_at DESC, rowid DESC LIMIT ?");
    stmt.bind(1, static_cast<std::int64_t>(limit));

    std::vector<AuditEntry> entries;
    while (stmt.step()) {
        AuditEntry entry;
        if (const auto parsed = nexus::core::Uuid::parse(stmt.column_text(0))) {
            entry.id = *parsed;
        }
        entry.actor = stmt.column_text(1);
        entry.action = stmt.column_text(2);
        entry.target = stmt.column_text(3);
        entry.detail = stmt.column_text(4);
        if (const auto ts = nexus::core::from_iso8601(stmt.column_text(5))) {
            entry.created_at = *ts;
        }
        entries.push_back(std::move(entry));
    }
    return entries;
}

} // namespace nexus::services
