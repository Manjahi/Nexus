#include "nexus/jobs/retry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <stdexcept>
#include <vector>

using namespace std::chrono_literals;
using nexus::jobs::delay_for_attempt;
using nexus::jobs::RetryPolicy;
using nexus::jobs::run_with_retry;

TEST_CASE("delay_for_attempt grows geometrically and caps", "[jobs][retry]") {
    const RetryPolicy policy{5, 100ms, 2.0, 1000ms};

    REQUIRE(delay_for_attempt(policy, 1) == 0ms);
    REQUIRE(delay_for_attempt(policy, 2) == 100ms);
    REQUIRE(delay_for_attempt(policy, 3) == 200ms);
    REQUIRE(delay_for_attempt(policy, 4) == 400ms);
    REQUIRE(delay_for_attempt(policy, 5) == 800ms);
    REQUIRE(delay_for_attempt(policy, 6) == 1000ms); // capped
}

TEST_CASE("RetryPolicy::none never delays", "[jobs][retry]") {
    REQUIRE(delay_for_attempt(RetryPolicy::none(), 2) == 0ms);
}

TEST_CASE("run_with_retry succeeds after transient failures", "[jobs][retry]") {
    std::vector<std::chrono::milliseconds> slept;
    int calls = 0;

    const int attempts = run_with_retry(
        [&] {
            if (++calls < 3) {
                throw std::runtime_error("transient");
            }
        },
        RetryPolicy{4, 10ms, 3.0, 1000ms},
        [&](std::chrono::milliseconds d) { slept.push_back(d); });

    REQUIRE(attempts == 3);
    REQUIRE(calls == 3);
    REQUIRE(slept == std::vector<std::chrono::milliseconds>{10ms, 30ms});
}

TEST_CASE("run_with_retry rethrows after exhausting attempts", "[jobs][retry]") {
    int calls = 0;
    REQUIRE_THROWS_AS(run_with_retry(
                          [&] {
                              ++calls;
                              throw std::runtime_error("always");
                          },
                          RetryPolicy{3, 1ms, 2.0, 10ms}, [](std::chrono::milliseconds) {}),
                      std::runtime_error);
    REQUIRE(calls == 3);
}

TEST_CASE("run_with_retry runs once on immediate success", "[jobs][retry]") {
    int calls = 0;
    const int attempts =
        run_with_retry([&] { ++calls; }, RetryPolicy{3, 1ms, 2.0, 10ms},
                       [](std::chrono::milliseconds) { FAIL("should not sleep"); });
    REQUIRE(attempts == 1);
    REQUIRE(calls == 1);
}
