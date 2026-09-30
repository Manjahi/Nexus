#include "nexus/core/result.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>

using nexus::core::Result;

namespace {
using IntResult = Result<int, std::string>;
}

TEST_CASE("Result carries a success value", "[core][result]") {
    const auto r = IntResult::ok(42);
    REQUIRE(r.has_value());
    REQUIRE(static_cast<bool>(r));
    REQUIRE(r.value() == 42);
}

TEST_CASE("Result carries an error value", "[core][result]") {
    const auto r = IntResult::err("boom");
    REQUIRE_FALSE(r.has_value());
    REQUIRE_FALSE(static_cast<bool>(r));
    REQUIRE(r.error() == "boom");
}

TEST_CASE("value_or falls back on the error path", "[core][result]") {
    REQUIRE(IntResult::ok(7).value_or(99) == 7);
    REQUIRE(IntResult::err("x").value_or(99) == 99);
}

TEST_CASE("Result equality compares state and payload", "[core][result]") {
    REQUIRE(IntResult::ok(1) == IntResult::ok(1));
    REQUIRE_FALSE(IntResult::ok(1) == IntResult::ok(2));
    REQUIRE_FALSE(IntResult::ok(1) == IntResult::err("1"));
}
