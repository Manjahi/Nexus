#include "nexus/fs/exclusion_rules.hpp"

#include <catch2/catch_test_macros.hpp>

using nexus::fs::ExclusionRules;
using nexus::fs::glob_match;

TEST_CASE("glob_match basics", "[fs][glob]") {
    REQUIRE(glob_match("*.tmp", "file.tmp"));
    REQUIRE(glob_match("*.tmp", "a/b/file.tmp"));
    REQUIRE_FALSE(glob_match("*.tmp", "file.tmpx"));
    REQUIRE(glob_match("build/*", "build/output.o"));
    REQUIRE(glob_match("a?c", "abc"));
    REQUIRE_FALSE(glob_match("a?c", "ac"));
    REQUIRE(glob_match("*", "anything/at/all"));
    REQUIRE(glob_match("CACHE*", "cache_v2")); // case-insensitive
    REQUIRE(glob_match("x", "X"));
}

TEST_CASE("directory-name rules prune and exclude", "[fs][exclusions]") {
    ExclusionRules rules;
    rules.exclude_directory_name("node_modules");

    REQUIRE(rules.prunes_directory("node_modules"));
    REQUIRE(rules.prunes_directory("NODE_MODULES"));
    REQUIRE_FALSE(rules.prunes_directory("src"));
    REQUIRE(rules.excludes_path("app/node_modules"));
    REQUIRE(rules.excludes_path("node_modules"));
}

TEST_CASE("glob rules match path and leaf", "[fs][exclusions]") {
    ExclusionRules rules;
    rules.exclude_glob("*.log");
    rules.exclude_glob("dist/*");

    REQUIRE(rules.excludes_path("logs/today.log"));
    REQUIRE(rules.excludes_path("dist/app.js"));
    REQUIRE_FALSE(rules.excludes_path("src/app.js"));
    REQUIRE(rules.prunes_directory("dist") == false); // "dist/*" has a slash
}

TEST_CASE("text round-trip and comments", "[fs][exclusions]") {
    const char* text = "# ignore list\n"
                       "node_modules\n"
                       "\n"
                       "*.bak\n"
                       "  build/**  \n";
    const auto rules = ExclusionRules::from_text(text);

    REQUIRE(rules.prunes_directory("node_modules"));
    REQUIRE(rules.excludes_path("x/y.bak"));
    REQUIRE(rules.excludes_path("build/deep/thing"));

    const auto again = ExclusionRules::from_text(rules.to_text());
    REQUIRE(again.excludes_path("x/y.bak"));
    REQUIRE(again.prunes_directory("node_modules"));
}

TEST_CASE("defaults cover common noise", "[fs][exclusions]") {
    const auto rules = ExclusionRules::defaults();
    REQUIRE(rules.prunes_directory(".git"));
    REQUIRE(rules.prunes_directory("node_modules"));
    REQUIRE(rules.excludes_path("obj/foo.obj"));
    REQUIRE(rules.excludes_path("weird/Thumbs.db"));
    REQUIRE_FALSE(rules.excludes_path("src/main.cpp"));
}
