#include "nexus/services/heavy_job_guard.hpp"

#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <utility>
#include <vector>

using nexus::services::HeavyJobGuard;

TEST_CASE("a fresh guard has no active jobs", "[services][heavy_job_guard]") {
    HeavyJobGuard guard;
    REQUIRE(guard.active().empty());
}

TEST_CASE("acquire registers a label until the lease is dropped", "[services][heavy_job_guard]") {
    HeavyJobGuard guard;
    {
        auto lease = guard.acquire("Storage scan");
        REQUIRE(guard.active() == std::vector<std::string>{"Storage scan"});
    }
    REQUIRE(guard.active().empty());
}

TEST_CASE("multiple leases can be held at once and each release is independent",
          "[services][heavy_job_guard]") {
    HeavyJobGuard guard;
    auto scan = guard.acquire("Storage scan");
    auto backup = guard.acquire("Backup: Nightly");
    REQUIRE(guard.active().size() == 2);

    scan = HeavyJobGuard::Lease{}; // drop the first lease early
    const auto active = guard.active();
    REQUIRE(active.size() == 1);
    REQUIRE(active[0] == "Backup: Nightly");
}

TEST_CASE("move assignment releases the destination's previous lease",
          "[services][heavy_job_guard]") {
    HeavyJobGuard guard;
    auto a = guard.acquire("A");
    auto b = guard.acquire("B");
    REQUIRE(guard.active().size() == 2);

    a = std::move(b); // 'A's slot must be released here, not leaked
    REQUIRE(guard.active() == std::vector<std::string>{"B"});
}

TEST_CASE("move construction transfers ownership without releasing",
          "[services][heavy_job_guard]") {
    HeavyJobGuard guard;
    auto a = guard.acquire("A");
    HeavyJobGuard::Lease moved(std::move(a));
    REQUIRE(guard.active() == std::vector<std::string>{"A"});
}

TEST_CASE("a default-constructed lease releases nothing", "[services][heavy_job_guard]") {
    HeavyJobGuard guard;
    {
        HeavyJobGuard::Lease empty;
        (void) empty;
    }
    REQUIRE(guard.active().empty()); // no crash, no phantom release
}

TEST_CASE("acquire/release under concurrent use never corrupts the holder list",
          "[services][heavy_job_guard]") {
    HeavyJobGuard guard;
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([&guard, i] {
            for (int j = 0; j < 200; ++j) {
                auto lease = guard.acquire("job-" + std::to_string(i));
                (void) guard.active();
            }
        });
    }
    for (auto& t : threads) {
        t.join();
    }
    REQUIRE(guard.active().empty());
}
