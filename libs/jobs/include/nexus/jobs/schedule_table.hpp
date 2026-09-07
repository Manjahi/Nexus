#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

namespace nexus::jobs {

using SchedulerClock = std::chrono::steady_clock;

/// Deterministic scheduling bookkeeping with no threads or time of its own.
/// The owning Scheduler feeds it the current time and dispatches the ids it
/// returns.
class ScheduleTable {
public:
    struct Id {
        std::uint64_t value = 0;
        friend bool operator==(const Id&, const Id&) = default;
    };

    /// Adds an entry firing first at `first_run`. If `interval` is set the entry
    /// repeats; otherwise it is one-shot.
    Id add(SchedulerClock::time_point first_run, std::optional<SchedulerClock::duration> interval);

    /// Removes an entry. Returns false if the id is unknown.
    bool cancel(Id id);

    [[nodiscard]] bool contains(Id id) const;
    [[nodiscard]] bool empty() const { return entries_.empty(); }
    [[nodiscard]] std::size_t size() const { return entries_.size(); }

    /// Returns, earliest-first, the ids due at or before `now`. Recurring entries
    /// are advanced to their next future run (skipping any missed intervals);
    /// one-shot entries are removed.
    std::vector<Id> collect_due(SchedulerClock::time_point now);

    /// The earliest scheduled run across all entries, or nullopt if empty.
    [[nodiscard]] std::optional<SchedulerClock::time_point> next_run() const;

private:
    struct Entry {
        Id id;
        SchedulerClock::time_point next_run;
        std::optional<SchedulerClock::duration> interval;
    };

    std::vector<Entry> entries_;
    std::uint64_t next_id_ = 1;
};

} // namespace nexus::jobs
