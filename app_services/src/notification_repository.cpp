#include "nexus/services/notification_repository.hpp"

#include "nexus/db/database.hpp"
#include "nexus/db/statement.hpp"
#include "nexus/notify/notification_center.hpp"
#include "nexus/notify/severity.hpp"
#include "support.hpp"

#include <string>
#include <utility>

namespace nexus::services {

using detail::bind_text_or_null;
using detail::column_time_or_null;

namespace {

nexus::notify::Notification read_notification(nexus::db::Statement& stmt) {
    nexus::notify::Notification note;
    if (const auto id = nexus::core::Uuid::parse(stmt.column_text(0))) {
        note.id = *id;
    }
    note.module = stmt.column_is_null(1) ? std::string{} : stmt.column_text(1);
    note.severity = nexus::notify::severity_from_string(stmt.column_text(2))
                        .value_or(nexus::notify::Severity::Info);
    note.title = stmt.column_text(3);
    note.body = stmt.column_is_null(4) ? std::string{} : stmt.column_text(4);
    if (const auto ts = nexus::core::from_iso8601(stmt.column_text(5))) {
        note.created_at = *ts;
    }
    note.read_at = column_time_or_null(stmt, 6);
    return note;
}

} // namespace

void NotificationRepository::insert(const nexus::notify::Notification& note) {
    nexus::db::Statement stmt = db_->prepare(
        "INSERT INTO notifications (id, module, severity, title, body, created_at, read_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)");
    stmt.bind(1, note.id.to_string());
    bind_text_or_null(stmt, 2, note.module);
    stmt.bind(3, nexus::notify::to_string(note.severity));
    stmt.bind(4, note.title);
    bind_text_or_null(stmt, 5, note.body);
    stmt.bind(6, nexus::core::to_iso8601(note.created_at));
    detail::bind_time_or_null(stmt, 7, note.read_at);
    stmt.step();
}

bool NotificationRepository::mark_read(const nexus::core::Uuid& id) {
    nexus::db::Statement stmt =
        db_->prepare("UPDATE notifications SET read_at = ? WHERE id = ? AND read_at IS NULL");
    stmt.bind(1, nexus::core::to_iso8601(nexus::core::now()));
    stmt.bind(2, id.to_string());
    stmt.step();
    return stmt.changes() > 0;
}

std::vector<nexus::notify::Notification> NotificationRepository::recent(std::size_t limit) const {
    nexus::db::Statement stmt = db_->prepare(
        "SELECT id, module, severity, title, body, created_at, read_at FROM notifications "
        "ORDER BY created_at DESC, rowid DESC LIMIT ?");
    stmt.bind(1, static_cast<std::int64_t>(limit));
    std::vector<nexus::notify::Notification> notes;
    while (stmt.step()) {
        notes.push_back(read_notification(stmt));
    }
    return notes;
}

std::size_t NotificationRepository::unread_count() const {
    nexus::db::Statement stmt =
        db_->prepare("SELECT COUNT(*) FROM notifications WHERE read_at IS NULL");
    stmt.step();
    return static_cast<std::size_t>(stmt.column_int64(0));
}

std::int64_t NotificationRepository::prune_before(nexus::core::Timestamp cutoff) {
    nexus::db::Statement stmt = db_->prepare("DELETE FROM notifications WHERE created_at < ?");
    stmt.bind(1, nexus::core::to_iso8601(cutoff));
    stmt.step();
    return db_->changes();
}

void attach_persistence(nexus::notify::NotificationCenter& center, NotificationRepository& repo,
                        std::size_t history_limit) {
    center.set_persist_sink(
        [&repo](const nexus::notify::Notification& note) { repo.insert(note); });
    center.seed(repo.recent(history_limit));
}

} // namespace nexus::services
