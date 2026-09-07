#include "nexus/services/notification_repository.hpp"

#include "nexus/db/database.hpp"
#include "nexus/db/migration.hpp"
#include "nexus/notify/notification_center.hpp"

#include <catch2/catch_test_macros.hpp>

using nexus::notify::NotificationCenter;
using nexus::notify::Severity;
using nexus::services::attach_persistence;
using nexus::services::NotificationRepository;

namespace {
nexus::db::Database migrated_db() {
    auto db = nexus::db::Database::open_in_memory();
    nexus::db::migrate(db, nexus::db::core_migrations());
    return db;
}
} // namespace

TEST_CASE("insert and read back a notification", "[services][notifications]") {
    auto db = migrated_db();
    NotificationRepository repo(db);

    nexus::notify::Notification note;
    note.id = nexus::core::Uuid::generate();
    note.module = "network";
    note.severity = Severity::Warning;
    note.title = "router unreachable";
    note.body = "no ARP reply for 30s";
    note.created_at = nexus::core::now();
    repo.insert(note);

    const auto rows = repo.recent();
    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].id == note.id);
    REQUIRE(rows[0].module == "network");
    REQUIRE(rows[0].severity == Severity::Warning);
    REQUIRE(rows[0].title == "router unreachable");
    REQUIRE_FALSE(rows[0].is_read());
    REQUIRE(repo.unread_count() == 1);
}

TEST_CASE("mark_read is idempotent and reflected in the count", "[services][notifications]") {
    auto db = migrated_db();
    NotificationRepository repo(db);

    nexus::notify::Notification note;
    note.id = nexus::core::Uuid::generate();
    note.severity = Severity::Info;
    note.title = "hello";
    note.created_at = nexus::core::now();
    repo.insert(note);

    REQUIRE(repo.mark_read(note.id));
    REQUIRE_FALSE(repo.mark_read(note.id));
    REQUIRE(repo.unread_count() == 0);
    REQUIRE(repo.recent()[0].is_read());
}

TEST_CASE("attach_persistence writes through and seeds history", "[services][notifications]") {
    auto db = migrated_db();
    NotificationRepository repo(db);

    {
        NotificationCenter first;
        attach_persistence(first, repo);
        first.post("storage", Severity::Info, "scan complete");
        first.post("backup", Severity::Error, "snapshot failed");
    }

    REQUIRE(repo.recent().size() == 2);
    REQUIRE(repo.unread_count() == 2);

    NotificationCenter reopened;
    attach_persistence(reopened, repo);
    REQUIRE(reopened.size() == 2);
    REQUIRE(reopened.recent()[0].title == "snapshot failed"); // newest first
    REQUIRE(reopened.unread_count() == 2);
}
