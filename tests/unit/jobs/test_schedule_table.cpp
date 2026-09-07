#include "nexus/jobs/schedule_table.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

using namespace std::chrono_literals;
using nexus::jobs::ScheduleTable;
using nexus::jobs::SchedulerClock;

namespace {
SchedulerClock::time_point t0() {
    static const SchedulerClock::time_point base = SchedulerClock::now();
    return base;
}
} // namespace

TEST_CASE("nothing is due before its time", "[jobs][schedule]") {
    ScheduleTable table;
    table.add(t0() + 100ms, std::nullopt);

    REQUIRE(table.collect_due(t0()).empty());
    REQUIRE(table.next_run() == t0() + 100ms);
    REQUIRE(table.size() == 1);
}

TEST_CASE("one-shot entries fire once and are removed", "[jobs][schedule]") {
    ScheduleTable table;
    const auto id = table.add(t0() + 10ms, std::nullopt);

    const auto fired = table.collect_due(t0() + 20ms);
    REQUIRE(fired.size() == 1);
    REQUIRE(fired[0] == id);
    REQUIRE(table.empty());
    REQUIRE_FALSE(table.contains(id));
    REQUIRE(table.next_run() == std::nullopt);
}

TEST_CASE("interval entries reschedule and survive", "[jobs][schedule]") {
    ScheduleTable table;
    const auto id = table.add(t0(), 50ms);

    REQUIRE(table.collect_due(t0()).size() == 1);
    REQUIRE(table.contains(id));
    REQUIRE(table.next_run() == t0() + 50ms);

    REQUIRE(table.collect_due(t0() + 10ms).empty()); // not yet
    REQUIRE(table.collect_due(t0() + 60ms).size() == 1);
    REQUIRE(table.next_run() == t0() + 100ms);
}

TEST_CASE("a long gap skips missed intervals instead of stacking", "[jobs][schedule]") {
    ScheduleTable table;
    table.add(t0(), 20ms);
    table.collect_due(t0());

    // Jump far ahead: should fire once, next run is the first future boundary.
    const auto fired = table.collect_due(t0() + 205ms);
    REQUIRE(fired.size() == 1);
    REQUIRE(table.next_run() == t0() + 220ms);
}

TEST_CASE("due ids come back earliest-first", "[jobs][schedule]") {
    ScheduleTable table;
    const auto late = table.add(t0() + 30ms, std::nullopt);
    const auto early = table.add(t0() + 10ms, std::nullopt);
    const auto mid = table.add(t0() + 20ms, std::nullopt);

    const auto fired = table.collect_due(t0() + 100ms);
    REQUIRE(fired == std::vector<ScheduleTable::Id>{early, mid, late});
}

TEST_CASE("cancel removes an entry", "[jobs][schedule]") {
    ScheduleTable table;
    const auto id = table.add(t0() + 10ms, 10ms);

    REQUIRE(table.cancel(id));
    REQUIRE_FALSE(table.cancel(id));
    REQUIRE(table.empty());
    REQUIRE(table.collect_due(t0() + 100ms).empty());
}
