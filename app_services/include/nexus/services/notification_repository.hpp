#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "nexus/core/id.hpp"
#include "nexus/core/time.hpp"
#include "nexus/notify/notification.hpp"

namespace nexus::db {
class Database;
}
namespace nexus::notify {
class NotificationCenter;
}

namespace nexus::services {

/// Persists notifications to the `notifications` table and reloads them.
class NotificationRepository {
public:
    explicit NotificationRepository(nexus::db::Database& db) noexcept : db_(&db) {}

    void insert(const nexus::notify::Notification& note);
    bool mark_read(const nexus::core::Uuid& id);

    [[nodiscard]] std::vector<nexus::notify::Notification> recent(std::size_t limit = 200) const;
    [[nodiscard]] std::size_t unread_count() const;

    /// Deletes notifications created before `cutoff`, read or not - old alerts
    /// stop being actionable regardless. Returns rows removed.
    std::int64_t prune_before(nexus::core::Timestamp cutoff);

private:
    nexus::db::Database* db_;
};

/// Wires a NotificationCenter to a repository: every new notification is
/// written through, and `history_limit` most-recent rows are loaded back into
/// the center. Call once at startup.
void attach_persistence(nexus::notify::NotificationCenter& center, NotificationRepository& repo,
                        std::size_t history_limit = 200);

} // namespace nexus::services
