#include "nexus/jobs/throttle.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

using namespace nexus::jobs;

TEST_CASE("to_string / throttle_level_from_string round-trip", "[jobs][throttle]") {
    for (const auto level :
        {ThrottleLevel::Unlimited, ThrottleLevel::High, ThrottleLevel::Normal, ThrottleLevel::Low}) {
        const auto parsed = throttle_level_from_string(to_string(level));
        REQUIRE(parsed.has_value());
        REQUIRE(*parsed == level);
    }
}

TEST_CASE("throttle_level_from_string rejects unknown text", "[jobs][throttle]") {
    REQUIRE_FALSE(throttle_level_from_string("").has_value());
    REQUIRE_FALSE(throttle_level_from_string("extreme").has_value());
    REQUIRE_FALSE(throttle_level_from_string("Normal").has_value()); // case-sensitive
}

TEST_CASE("Unlimited never sleeps", "[jobs][throttle]") {
    REQUIRE(Throttle::delay_for(ThrottleLevel::Unlimited) == std::chrono::milliseconds{0});

    Throttle throttle(ThrottleLevel::Unlimited);
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 50; ++i) {
        throttle.pace();
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    REQUIRE(elapsed < std::chrono::milliseconds{50});
}

TEST_CASE("higher throttle levels sleep longer per pace() call", "[jobs][throttle]") {
    REQUIRE(Throttle::delay_for(ThrottleLevel::High) < Throttle::delay_for(ThrottleLevel::Normal));
    REQUIRE(Throttle::delay_for(ThrottleLevel::Normal) < Throttle::delay_for(ThrottleLevel::Low));
}

TEST_CASE("pace() actually sleeps at least the configured delay", "[jobs][throttle]") {
    Throttle throttle(ThrottleLevel::Normal);
    const auto expected = Throttle::delay_for(ThrottleLevel::Normal);

    const auto start = std::chrono::steady_clock::now();
    throttle.pace();
    const auto elapsed = std::chrono::steady_clock::now() - start;

    REQUIRE(elapsed >= expected);
}

TEST_CASE("level() reports the constructed level", "[jobs][throttle]") {
    REQUIRE(Throttle(ThrottleLevel::Low).level() == ThrottleLevel::Low);
    REQUIRE(Throttle().level() == ThrottleLevel::Unlimited);
}
