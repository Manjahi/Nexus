#include "nexus/jobs/schedule_table.hpp"

#include <algorithm>

namespace nexus::jobs {

ScheduleTable::Id ScheduleTable::add(SchedulerClock::time_point first_run,
                                     std::optional<SchedulerClock::duration> interval) {
    const Id id{next_id_++};
    entries_.push_back(Entry{id, first_run, interval});
    return id;
}

bool ScheduleTable::cancel(Id id) {
    const auto it = std::find_if(entries_.begin(), entries_.end(),
                                 [&](const Entry& e) { return e.id == id; });
    if (it == entries_.end()) {
        return false;
    }
    entries_.erase(it);
    return true;
}

bool ScheduleTable::contains(Id id) const {
    return std::any_of(entries_.begin(), entries_.end(),
                       [&](const Entry& e) { return e.id == id; });
}

std::vector<ScheduleTable::Id> ScheduleTable::collect_due(SchedulerClock::time_point now) {
    std::vector<Entry*> due;
    for (Entry& e : entries_) {
        if (e.next_run <= now) {
            due.push_back(&e);
        }
    }
    std::sort(due.begin(), due.end(),
              [](const Entry* a, const Entry* b) { return a->next_run < b->next_run; });

    std::vector<Id> fired;
    fired.reserve(due.size());
    for (Entry* e : due) {
        fired.push_back(e->id);
        if (e->interval && *e->interval > SchedulerClock::duration::zero()) {
            do {
                e->next_run += *e->interval;
            } while (e->next_run <= now);
        } else {
            e->next_run = SchedulerClock::time_point::max(); // mark one-shot for removal
        }
    }

    std::erase_if(entries_, [](const Entry& e) {
        return e.next_run == SchedulerClock::time_point::max();
    });

    return fired;
}

std::optional<SchedulerClock::time_point> ScheduleTable::next_run() const {
    if (entries_.empty()) {
        return std::nullopt;
    }
    auto it = std::min_element(entries_.begin(), entries_.end(),
                               [](const Entry& a, const Entry& b) {
                                   return a.next_run < b.next_run;
                               });
    return it->next_run;
}

} // namespace nexus::jobs
