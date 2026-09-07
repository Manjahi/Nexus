#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "nexus/notify/notification.hpp"

namespace nexus::notify {

/// In-process hub for module notifications (UFR-005). Keeps a bounded, newest-
/// first history, fans out to observers, and optionally forwards each new
/// notification to a persistence sink. All methods are thread-safe; observers
/// and the sink run outside the internal lock.
class NotificationCenter {
public:
    using Observer = std::function<void(const Notification&)>;
    using PersistSink = std::function<void(const Notification&)>;

    struct SubscriptionId {
        std::uint64_t value = 0;
        friend bool operator==(const SubscriptionId&, const SubscriptionId&) = default;
    };

    explicit NotificationCenter(std::size_t history_limit = 500);

    /// Publishes a notification and returns its id.
    nexus::core::Uuid post(std::string module, Severity severity, std::string title,
                           std::string body = {});

    bool mark_read(const nexus::core::Uuid& id);
    std::size_t mark_all_read();

    [[nodiscard]] std::vector<Notification> recent(std::size_t limit = 50) const;
    [[nodiscard]] std::vector<Notification> unread() const;
    [[nodiscard]] std::size_t unread_count() const;
    [[nodiscard]] std::size_t size() const;

    SubscriptionId subscribe(Observer observer);
    bool unsubscribe(SubscriptionId id);

    /// Sets (or clears, with {}) the sink invoked once per new notification.
    void set_persist_sink(PersistSink sink);

private:
    mutable std::mutex mutex_;
    std::deque<Notification> history_;
    std::size_t history_limit_;
    std::unordered_map<std::uint64_t, Observer> observers_;
    std::uint64_t next_subscription_ = 1;
    PersistSink persist_sink_;
};

} // namespace nexus::notify
