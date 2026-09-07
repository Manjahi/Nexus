#include "nexus/core/time.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>

using namespace std::chrono;
using nexus::core::from_iso8601;
using nexus::core::to_iso8601;

TEST_CASE("to_iso8601 formats a known epoch instant", "[core][time]") {
    // 2021-01-01T00:00:00Z == 1609459200 seconds since the Unix epoch.
    const nexus::core::Timestamp tp{seconds{1609459200}};
    REQUIRE(to_iso8601(tp) == "2021-01-01T00:00:00Z");
}

TEST_CASE("to_iso8601 output has the expected shape", "[core][time]") {
    const std::string s = to_iso8601(nexus::core::now());
    REQUIRE(s.size() == 20);
    REQUIRE(s[4] == '-');
    REQUIRE(s[7] == '-');
    REQUIRE(s[10] == 'T');
    REQUIRE(s[13] == ':');
    REQUIRE(s[16] == ':');
    REQUIRE(s[19] == 'Z');
}

TEST_CASE("from_iso8601 parses a known instant", "[core][time]") {
    const auto parsed = from_iso8601("2021-01-01T00:00:00Z");
    REQUIRE(parsed.has_value());
    REQUIRE(*parsed == nexus::core::Timestamp{seconds{1609459200}});
}

TEST_CASE("from_iso8601 round-trips to_iso8601", "[core][time]") {
    const auto original = time_point_cast<seconds>(nexus::core::now());
    const auto parsed = from_iso8601(to_iso8601(original));
    REQUIRE(parsed.has_value());
    REQUIRE(time_point_cast<seconds>(*parsed) == original);
}

TEST_CASE("from_iso8601 rejects malformed input", "[core][time]") {
    REQUIRE_FALSE(from_iso8601("").has_value());
    REQUIRE_FALSE(from_iso8601("2021-01-01T00:00:00").has_value());     // no Z
    REQUIRE_FALSE(from_iso8601("2021-01-01 00:00:00Z").has_value());    // space, not T
    REQUIRE_FALSE(from_iso8601("2021-13-01T00:00:00Z").has_value());    // bad month
    REQUIRE_FALSE(from_iso8601("2021-01-01T00:00:00.500Z").has_value()); // sub-second
}
