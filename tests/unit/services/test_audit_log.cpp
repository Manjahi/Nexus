#include "nexus/db/database.hpp"
#include "nexus/db/migration.hpp"
#include "nexus/db/statement.hpp"
#include "nexus/services/audit_log.hpp"

#include <catch2/catch_test_macros.hpp>

using nexus::services::AuditEntry;
using nexus::services::AuditLog;

namespace {
nexus::db::Database migrated_db() {
    auto db = nexus::db::Database::open_in_memory();
    nexus::db::migrate(db, "core", nexus::db::core_migrations());
    return db;
}
} // namespace

TEST_CASE("record persists an entry and returns its id", "[services][audit]") {
    auto db = migrated_db();
    AuditLog audit(db);

    const auto id = audit.record("delete_duplicates", "C:/tmp", "42 files", "storage-module");

    auto stmt = db.prepare("SELECT actor, action, target, detail FROM audit_logs WHERE id = ?");
    stmt.bind(1, id.to_string());
    REQUIRE(stmt.step());
    REQUIRE(stmt.column_text(0) == "storage-module");
    REQUIRE(stmt.column_text(1) == "delete_duplicates");
    REQUIRE(stmt.column_text(2) == "C:/tmp");
    REQUIRE(stmt.column_text(3) == "42 files");
}

TEST_CASE("empty optional fields are stored as NULL", "[services][audit]") {
    auto db = migrated_db();
    AuditLog audit(db);
    const auto id = audit.record("app_start");

    auto stmt = db.prepare(
        "SELECT actor IS NULL, target IS NULL, detail IS NULL FROM audit_logs WHERE id = ?");
    stmt.bind(1, id.to_string());
    REQUIRE(stmt.step());
    REQUIRE(stmt.column_int64(0) == 1);
    REQUIRE(stmt.column_int64(1) == 1);
    REQUIRE(stmt.column_int64(2) == 1);
}

TEST_CASE("recent returns entries newest-first with parsed timestamps", "[services][audit]") {
    auto db = migrated_db();
    AuditLog audit(db);
    audit.record("first");
    audit.record("second");
    audit.record("third");

    const auto entries = audit.recent(2);
    REQUIRE(entries.size() == 2);
    REQUIRE(entries[0].action == "third");
    REQUIRE(entries[1].action == "second");
    REQUIRE(entries[0].created_at.time_since_epoch().count() > 0);
    REQUIRE_FALSE(entries[0].id.is_nil());
}
