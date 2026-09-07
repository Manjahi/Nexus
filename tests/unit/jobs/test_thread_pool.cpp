#include "nexus/jobs/thread_pool.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <vector>

using nexus::jobs::ThreadPool;
using namespace std::chrono_literals;

TEST_CASE("pool runs every submitted task", "[jobs][threadpool]") {
    ThreadPool pool(4);
    std::atomic<int> sum{0};

    std::vector<std::future<void>> futures;
    for (int i = 1; i <= 100; ++i) {
        futures.push_back(pool.submit([&sum, i] { sum.fetch_add(i); }));
    }
    for (auto& f : futures) {
        f.get();
    }
    REQUIRE(sum.load() == 5050);
}

TEST_CASE("wait_idle blocks until the queue drains", "[jobs][threadpool]") {
    ThreadPool pool(2);
    std::atomic<int> done{0};
    for (int i = 0; i < 20; ++i) {
        pool.submit([&done] {
            std::this_thread::sleep_for(1ms);
            done.fetch_add(1);
        });
    }
    pool.wait_idle();
    REQUIRE(done.load() == 20);
    REQUIRE(pool.pending() == 0);
}

TEST_CASE("a throwing task surfaces through its future without killing the pool",
          "[jobs][threadpool]") {
    ThreadPool pool(2);

    auto bad = pool.submit([] { throw std::runtime_error("boom"); });
    REQUIRE_THROWS_AS(bad.get(), std::runtime_error);

    std::atomic<bool> ran{false};
    pool.submit([&ran] { ran.store(true); }).get();
    REQUIRE(ran.load());
}

TEST_CASE("destructor drains queued work", "[jobs][threadpool]") {
    std::atomic<int> count{0};
    {
        ThreadPool pool(2);
        for (int i = 0; i < 50; ++i) {
            pool.submit([&count] {
                std::this_thread::sleep_for(200us);
                count.fetch_add(1);
            });
        }
    }
    REQUIRE(count.load() == 50);
}

TEST_CASE("size reports the worker count", "[jobs][threadpool]") {
    ThreadPool pool(3);
    REQUIRE(pool.size() == 3);
    ThreadPool defaulted(0);
    REQUIRE(defaulted.size() >= 1);
}
