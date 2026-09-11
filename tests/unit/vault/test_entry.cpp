#include "nexus/vault/entry.hpp"

#include "nexus/core/time.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace nexus::vault;

namespace {
Entry sample_entry() {
    Entry e;
    e.id = "abc-123";
    e.title = "Example";
    e.username = "alice";
    e.password = "s3cret!";
    e.url = "https://example.com";
    e.notes = "some notes";
    e.tags = {"work", "email"};
    e.created_at = nexus::core::now();
    e.updated_at = e.created_at;
    return e;
}
} // namespace

TEST_CASE("secure_clear empties every field", "[vault][entry]") {
    Entry e = sample_entry();
    secure_clear(e);

    REQUIRE(e.id.empty());
    REQUIRE(e.title.empty());
    REQUIRE(e.username.empty());
    REQUIRE(e.password.empty());
    REQUIRE(e.url.empty());
    REQUIRE(e.notes.empty());
    REQUIRE(e.tags.empty());
}

TEST_CASE("serialize_entries / parse_entries round-trip", "[vault][entry]") {
    const std::vector<Entry> entries{sample_entry()};
    const std::string json = serialize_entries(entries);

    const auto parsed = parse_entries(json);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->size() == 1);
    REQUIRE((*parsed)[0].id == "abc-123");
    REQUIRE((*parsed)[0].title == "Example");
    REQUIRE((*parsed)[0].username == "alice");
    REQUIRE((*parsed)[0].password == "s3cret!");
    REQUIRE((*parsed)[0].url == "https://example.com");
    REQUIRE((*parsed)[0].notes == "some notes");
    REQUIRE((*parsed)[0].tags == std::vector<std::string>{"work", "email"});
}

TEST_CASE("serialize_entries of an empty list round-trips to an empty list", "[vault][entry]") {
    const auto parsed = parse_entries(serialize_entries({}));
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->empty());
}

TEST_CASE("parse_entries rejects malformed JSON", "[vault][entry]") {
    REQUIRE_FALSE(parse_entries("not json").has_value());
    REQUIRE_FALSE(parse_entries("{}").has_value()); // an object, not an array
    REQUIRE_FALSE(parse_entries("[{\"title\":\"missing id\"}]").has_value());
}
