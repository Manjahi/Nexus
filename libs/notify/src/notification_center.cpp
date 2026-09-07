#include "nexus/notify/notification_center.hpp"

#include <algorithm>
#include <utility>

#include "nexus/core/time.hpp"

namespace nexus::notify {

NotificationCenter::NotificationCenter(std::size_t history_limit)
    : history_limit_(history_limit == 0 ? 1 : history_limit) {}

nexus::core::Uuid NotificationCenter::post(std::string module, Severity severity, std::string title,
                                          std::string body) {
    Notification note;
    note.id = nexus::core::Uuid::generate();
    note.module = std::move(module);
    note.severity = severity;
    note.title = std::move(title);
    note.body = std::move(body);
    note.created_at = nexus::core::now();

    std::vector<Observer> observers;
    PersistSink sink;
    {
        std::scoped_lock lock(mutex_);
        history_.push_front(note);
        while (history_.size() > history_limit_) {
            history_.pop_back();
        }
        observers.reserve(observers_.size());
        for (const auto& entry : observers_) {
            observers.push_back(entry.second);
        }
        sink = persist_sink_;
    }

    if (sink) {
        sink(note);
    }
    for (const auto& observer : observers) {
        observer(note);
    }
    return note.id;
}

bool NotificationCenter::mark_read(const nexus::core::Uuid& id) {
    std::scoped_lock lock(mutex_);
    const auto it = std::find_if(history_.begin(), history_.end(),
                                 [&](const Notification& n) { return n.id == id; });
    if (it == history_.end() || it->read_at.has_value()) {
        return false;
    }
    it->read_at = nexus::core::now();
    return true;
}

std::size_t NotificationCenter::mark_all_read() {
    std::scoped_lock lock(mutex_);
    const auto stamp = nexus::core::now();
    std::size_t changed = 0;
    for (Notification& n : history_) {
        if (!n.read_at.has_value()) {
            n.read_at = stamp;
            ++changed;
        }
    }
    return changed;
}

std::vector<Notification> NotificationCenter::recent(std::size_t limit) const {
    std::scoped_lock lock(mutex_);
    const std::size_t count = std::min(limit, history_.size());
    return std::vector<Notification>(history_.begin(),
                                     history_.begin() + static_cast<std::ptrdiff_t>(count));
}

std::vector<Notification> NotificationCenter::unread() const {
    std::scoped_lock lock(mutex_);
    std::vector<Notification> out;
    for (const Notification& n : history_) {
        if (!n.read_at.has_value()) {
            out.push_back(n);
        }
    }
    return out;
}

std::size_t NotificationCenter::unread_count() const {
    std::scoped_lock lock(mutex_);
    return static_cast<std::size_t>(std::count_if(
        history_.begin(), history_.end(),
        [](const Notification& n) { return !n.read_at.has_value(); }));
}

std::size_t NotificationCenter::size() const {
    std::scoped_lock lock(mutex_);
    return history_.size();
}

NotificationCenter::SubscriptionId NotificationCenter::subscribe(Observer observer) {
    std::scoped_lock lock(mutex_);
    const SubscriptionId id{next_subscription_++};
    observers_.emplace(id.value, std::move(observer));
    return id;
}

bool NotificationCenter::unsubscribe(SubscriptionId id) {
    std::scoped_lock lock(mutex_);
    return observers_.erase(id.value) > 0;
}

void NotificationCenter::set_persist_sink(PersistSink sink) {
    std::scoped_lock lock(mutex_);
    persist_sink_ = std::move(sink);
}

} // namespace nexus::notify
