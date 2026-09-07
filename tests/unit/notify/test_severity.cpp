#include "nexus/notify/severity.hpp"

#include <catch2/catch_test_macros.hpp>

using nexus::notify::Severity;
using nexus::notify::severity_from_string;
using nexus::notify::to_string;

TEST_CASE("severity round-trips through its string form", "[notify][severity]") {
    for (const Severity s : {Severity::Info, Severity::Success, Severity::Warning, Severity::Error}) {
        const auto parsed = severity_from_string(to_string(s));
        REQUIRE(parsed.has_value());
        REQUIRE(*parsed == s);
    }
}

TEST_CASE("severity strings are stable identifiers", "[notify][severity]") {
    REQUIRE(to_string(Severity::Info) == "info");
    REQUIRE(to_string(Severity::Success) == "success");
    REQUIRE(to_string(Severity::Warning) == "warning");
    REQUIRE(to_string(Severity::Error) == "error");
}

TEST_CASE("unknown severity string yields nullopt", "[notify][severity]") {
    REQUIRE_FALSE(severity_from_string("").has_value());
    REQUIRE_FALSE(severity_from_string("critical").has_value());
}
