#include "nexus/jobs/scheduler.hpp"

#include "nexus/jobs/thread_pool.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <thread>

using namespace std::chrono_literals;
using nexus::jobs::Scheduler;
using nexus::jobs::ThreadPool;

namespace {

// Polls `pred` until it holds or the timeout elapses. Timing tests use this
// instead of fixed sleeps so they stay reliable under load.
template <class Pred>
bool wait_until(Pred pred, std::chrono::milliseconds timeout = 5s) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) {
            return true;
        }
        std::this_thread::sleep_for(2ms);
    }
    return pred();
}

} // namespace

TEST_CASE("schedule_after fires exactly once", "[jobs][scheduler]") {
    ThreadPool pool(2);
    Scheduler scheduler(pool);
    std::atomic<int> hits{0};

    scheduler.schedule_after(20ms, [&hits] { hits.fetch_add(1); });

    REQUIRE(wait_until([&] { return hits.load() >= 1; }));
    std::this_thread::sleep_for(100ms); // a spurious repeat would land here
    pool.wait_idle();
    REQUIRE(hits.load() == 1);
}

TEST_CASE("schedule_every fires repeatedly until cancelled", "[jobs][scheduler]") {
    ThreadPool pool(2);
    Scheduler scheduler(pool);
    std::atomic<int> hits{0};

    const auto id = scheduler.schedule_every(20ms, [&hits] { hits.fetch_add(1); });

    REQUIRE(wait_until([&] { return hits.load() >= 3; }));
    REQUIRE(scheduler.cancel(id));
    const int at_cancel = hits.load();

    std::this_thread::sleep_for(120ms);
    pool.wait_idle();
    REQUIRE(hits.load() <= at_cancel + 2); // only already-dispatched runs may slip through
}

TEST_CASE("cancel before the first fire suppresses the action", "[jobs][scheduler]") {
    ThreadPool pool(2);
    Scheduler scheduler(pool);
    std::atomic<int> hits{0};

    const auto id = scheduler.schedule_after(80ms, [&hits] { hits.fetch_add(1); });
    REQUIRE(scheduler.cancel(id));

    std::this_thread::sleep_for(200ms);
    pool.wait_idle();
    REQUIRE(hits.load() == 0);
}

TEST_CASE("stop halts further fires", "[jobs][scheduler]") {
    ThreadPool pool(2);
    Scheduler scheduler(pool);
    std::atomic<int> hits{0};

    scheduler.schedule_every(15ms, [&hits] { hits.fetch_add(1); });
    REQUIRE(wait_until([&] { return hits.load() >= 2; }));

    scheduler.stop();
    pool.wait_idle();
    const int after_stop = hits.load();

    std::this_thread::sleep_for(120ms);
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
