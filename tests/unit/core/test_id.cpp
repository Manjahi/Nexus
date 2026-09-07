#include "nexus/core/id.hpp"

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>

using nexus::core::Uuid;

TEST_CASE("generated UUIDs are well formed v4", "[core][id]") {
    const Uuid id = Uuid::generate();
    const std::string text = id.to_string();

    REQUIRE(text.size() == 36);
    REQUIRE(text[8] == '-');
    REQUIRE(text[13] == '-');
    REQUIRE(text[18] == '-');
    REQUIRE(text[23] == '-');
    REQUIRE(text[14] == '4');                       // version nibble
    REQUIRE((text[19] == '8' || text[19] == '9' ||  // variant nibble
             text[19] == 'a' || text[19] == 'b'));
    REQUIRE_FALSE(id.is_nil());
}

TEST_CASE("parse and to_string round-trip", "[core][id]") {
    const Uuid original = Uuid::generate();
    const auto parsed = Uuid::parse(original.to_string());

    REQUIRE(parsed.has_value());
    REQUIRE(*parsed == original);
}

TEST_CASE("parse rejects malformed input", "[core][id]") {
    REQUIRE_FALSE(Uuid::parse("").has_value());
    REQUIRE_FALSE(Uuid::parse("not-a-uuid").has_value());
    REQUIRE_FALSE(Uuid::parse("00000000-0000-0000-0000-00000000000g").has_value());
    REQUIRE_FALSE(Uuid::parse("000000000000000000000000000000000000").has_value());
}

TEST_CASE("generate does not collide across many draws", "[core][id]") {
    std::set<std::string> seen;
    for (int i = 0; i < 10'000; ++i) {
        REQUIRE(seen.insert(Uuid::generate().to_string()).second);
    }
}
