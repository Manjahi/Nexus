#include "nexus/core/time.hpp"
#include "nexus/vault/entry.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

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

TEST_CASE("entry kind round-trips through JSON", "[vault][entry]") {
    Entry note = sample_entry();
    note.kind = EntryKind::SecureNote;
    const auto parsed = entry_from_json(to_json(note));
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->kind == EntryKind::SecureNote);
}

// The migration story for every entry written before this enum existed:
// no "kind" field at all in the stored JSON, and it was always a password
// entry, so a missing field must default to Password rather than fail to
// parse or silently become something else.
TEST_CASE("an entry with no kind field defaults to Password", "[vault][entry]") {
    const auto parsed = entry_from_json(nlohmann::json{{"id", "legacy-1"}, {"title", "Old"}});
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->kind == EntryKind::Password);
}

TEST_CASE("entry_kind_from_string maps unrecognized text to Password", "[vault][entry]") {
    REQUIRE(entry_kind_from_string("password") == EntryKind::Password);
    REQUIRE(entry_kind_from_string("secure_note") == EntryKind::SecureNote);
    REQUIRE(entry_kind_from_string("garbage") == EntryKind::Password);
    REQUIRE(entry_kind_from_string("") == EntryKind::Password);
}
