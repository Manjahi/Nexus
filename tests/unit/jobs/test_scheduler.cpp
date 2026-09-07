#include "nexus/jobs/scheduler.hpp"

#include "nexus/jobs/thread_pool.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <thread>

using namespace std::chrono_literals;
using nexus::jobs::Scheduler;
using nexus::jobs::ThreadPool;

TEST_CASE("schedule_after fires exactly once", "[jobs][scheduler]") {
    ThreadPool pool(2);
    Scheduler scheduler(pool);
    std::atomic<int> hits{0};

    scheduler.schedule_after(20ms, [&hits] { hits.fetch_add(1); });
    std::this_thread::sleep_for(120ms);
    pool.wait_idle();

    REQUIRE(hits.load() == 1);
}

TEST_CASE("schedule_every fires repeatedly until cancelled", "[jobs][scheduler]") {
    ThreadPool pool(2);
    Scheduler scheduler(pool);
    std::atomic<int> hits{0};

    const auto id = scheduler.schedule_every(20ms, [&hits] { hits.fetch_add(1); });
    std::this_thread::sleep_for(155ms);
    const int at_cancel = hits.load();
    REQUIRE(scheduler.cancel(id));

    std::this_thread::sleep_for(80ms);
    pool.wait_idle();

    REQUIRE(at_cancel >= 3);
    REQUIRE(hits.load() <= at_cancel + 1); // at most one already-dispatched run slips through
}

TEST_CASE("cancel before the first fire suppresses the action", "[jobs][scheduler]") {
    ThreadPool pool(2);
    Scheduler scheduler(pool);
    std::atomic<int> hits{0};

    const auto id = scheduler.schedule_after(60ms, [&hits] { hits.fetch_add(1); });
    REQUIRE(scheduler.cancel(id));

    std::this_thread::sleep_for(120ms);
    pool.wait_idle();
    REQUIRE(hits.load() == 0);
}

TEST_CASE("stop halts further fires", "[jobs][scheduler]") {
    ThreadPool pool(2);
    Scheduler scheduler(pool);
    std::atomic<int> hits{0};

    scheduler.schedule_every(15ms, [&hits] { hits.fetch_add(1); });
    std::this_thread::sleep_for(70ms);
    scheduler.stop();
    const int after_stop = hits.load();

    std::this_thread::sleep_for(80ms);
    REQUIRE(hits.load() == after_stop);
}

TEST_CASE("destruction with a pending schedule does not hang", "[jobs][scheduler]") {
    ThreadPool pool(2);
    {
        Scheduler scheduler(pool);
        scheduler.schedule_after(10s, [] {});
        scheduler.schedule_every(5s, [] {});
    }
    SUCCEED();
}
