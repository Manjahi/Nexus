#pragma once

#include "nexus/jobs/schedule_table.hpp"

#include <condition_variable>
#include <functional>
#include <mutex>
#include <stop_token>
#include <thread>
#include <unordered_map>

namespace nexus::jobs {

class ThreadPool;

/// Timer thread that fires actions on the supplied ThreadPool. One-shot and
/// fixed-interval schedules are supported; interval actions that overrun simply
/// fire again at the next boundary (missed boundaries are skipped, not queued).
class Scheduler {
public:
    using Id = ScheduleTable::Id;

    explicit Scheduler(ThreadPool& pool);
    ~Scheduler();

    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    Id schedule_after(SchedulerClock::duration delay, std::function<void()> action);
    Id schedule_every(SchedulerClock::duration interval, std::function<void()> action,
                      SchedulerClock::duration initial_delay = SchedulerClock::duration::zero());

    bool cancel(Id id);

    /// Stops the timer thread. Idempotent; also called by the destructor.
    void stop();

private:
    void run(const std::stop_token& stop);
    Id add_locked(SchedulerClock::time_point first_run,
                  std::optional<SchedulerClock::duration> interval, std::function<void()> action);

    ThreadPool* pool_;
    std::mutex mutex_;
    std::condition_variable_any cv_;
    ScheduleTable table_;
    std::unordered_map<std::uint64_t, std::function<void()>> actions_;
    std::jthread thread_;
};

} // namespace nexus::jobs
