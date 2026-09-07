#include "nexus/core/time.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>

using namespace std::chrono;
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
