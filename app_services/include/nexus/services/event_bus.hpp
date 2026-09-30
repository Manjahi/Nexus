#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nexus::services {

/// Type-keyed publish/subscribe hub for decoupled cross-module signals
/// (spec section 9). Handlers are invoked synchronously on the publishing
/// thread, outside the internal lock, in subscription order.
class EventBus {
public:
    struct Token {
        std::type_index type{typeid(void)};
        std::uint64_t value = 0;
    };

    template <class Event, class Fn> Token subscribe(Fn&& handler) {
        auto boxed = std::make_shared<std::function<void(const Event&)>>(std::forward<Fn>(handler));
        const std::type_index key{typeid(Event)};
        const std::scoped_lock lock(mutex_);
        const std::uint64_t id = next_id_++;
        entries_[key].push_back(Entry{id, std::move(boxed)});
        return Token{key, id};
    }

    template <class Event> void publish(const Event& event) const {
        const std::type_index key{typeid(Event)};
        std::vector<std::shared_ptr<void>> targets;
        {
            const std::scoped_lock lock(mutex_);
            const auto it = entries_.find(key);
            if (it == entries_.end()) {
                return;
            }
            targets.reserve(it->second.size());
            for (const Entry& e : it->second) {
                targets.push_back(e.callable);
            }
        }
        for (const std::shared_ptr<void>& raw : targets) {
            const auto& fn = *std::static_pointer_cast<std::function<void(const Event&)>>(raw);
            fn(event);
        }
    }

    bool unsubscribe(const Token& token) {
        const std::scoped_lock lock(mutex_);
        const auto it = entries_.find(token.type);
        if (it == entries_.end()) {
            return false;
        }
        const std::size_t before = it->second.size();
        std::erase_if(it->second, [&](const Entry& e) { return e.id == token.value; });
        return it->second.size() != before;
    }

    [[nodiscard]] std::size_t handler_count() const {
        const std::scoped_lock lock(mutex_);
        std::size_t total = 0;
        for (const auto& bucket : entries_) {
            total += bucket.second.size();
        }
        return total;
    }

private:
    struct Entry {
        std::uint64_t id;
        std::shared_ptr<void> callable;
    };

    mutable std::mutex mutex_;
    std::unordered_map<std::type_index, std::vector<Entry>> entries_;
    std::uint64_t next_id_ = 1;
};

} // namespace nexus::services
