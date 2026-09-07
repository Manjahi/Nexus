#include "nexus/jobs/scheduler.hpp"

#include "nexus/jobs/thread_pool.hpp"

#include <utility>
#include <vector>

namespace nexus::jobs {

Scheduler::Scheduler(ThreadPool& pool) : pool_(&pool) {
    thread_ = std::jthread([this](std::stop_token stop) { run(stop); });
}

Scheduler::~Scheduler() {
    stop();
}

void Scheduler::stop() {
    thread_.request_stop();
    cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

Scheduler::Id Scheduler::add_locked(SchedulerClock::time_point first_run,
                                    std::optional<SchedulerClock::duration> interval,
                                    std::function<void()> action) {
    const Id id = table_.add(first_run, interval);
    actions_.emplace(id.value, std::move(action));
    cv_.notify_all();
    return id;
}

Scheduler::Id Scheduler::schedule_after(SchedulerClock::duration delay,
                                        std::function<void()> action) {
    std::scoped_lock lock(mutex_);
    return add_locked(SchedulerClock::now() + delay, std::nullopt, std::move(action));
}

Scheduler::Id Scheduler::schedule_every(SchedulerClock::duration interval,
                                        std::function<void()> action,
                                        SchedulerClock::duration initial_delay) {
    std::scoped_lock lock(mutex_);
    return add_locked(SchedulerClock::now() + initial_delay, interval, std::move(action));
}

bool Scheduler::cancel(Id id) {
    std::scoped_lock lock(mutex_);
    actions_.erase(id.value);
    const bool removed = table_.cancel(id);
    cv_.notify_all();
    return removed;
}

void Scheduler::run(const std::stop_token& stop) {
    // Wake the wait below whenever a stop is requested (covers the jthread
    // destructor path in addition to the explicit notify in stop()).
    const std::stop_callback wake(stop, [this] { cv_.notify_all(); });

    std::unique_lock lock(mutex_);
    while (!stop.stop_requested()) {
        const auto now = SchedulerClock::now();

        std::vector<std::function<void()>> to_dispatch;
        for (const Id id : table_.collect_due(now)) {
            const auto it = actions_.find(id.value);
            if (it == actions_.end()) {
                continue;
            }
            to_dispatch.push_back(it->second);
            if (!table_.contains(id)) {
                actions_.erase(it); // one-shot: drop its action
            }
        }

        if (!to_dispatch.empty()) {
            lock.unlock();
            for (auto& action : to_dispatch) {
                pool_->submit(std::move(action));
            }
            lock.lock();
            continue; // re-evaluate; more may now be due
        }

        if (const auto next = table_.next_run()) {
            cv_.wait_until(lock, *next);
        } else {
            cv_.wait(lock);
        }
    }
}

} // namespace nexus::jobs
