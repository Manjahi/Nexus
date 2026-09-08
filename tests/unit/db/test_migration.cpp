#include "nexus/db/database.hpp"
#include "nexus/db/migration.hpp"
#include "nexus/db/statement.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <vector>

using nexus::db::Database;
using nexus::db::Migration;

namespace {

bool table_exists(Database& db, std::string_view name) {
    auto stmt = db.prepare("SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ?");
    stmt.bind(1, name);
    return stmt.step();
}

} // namespace

TEST_CASE("core migrations create the shared schema", "[db][migration]") {
    Database db = Database::open_in_memory();

    const int version = nexus::db::migrate(db, "core", nexus::db::core_migrations());
    REQUIRE(version == 1);
    REQUIRE(nexus::db::schema_version(db, "core") == 1);

    for (const auto* t : {"app_settings", "machines", "jobs", "job_runs", "notifications",
                          "audit_logs", "reports"}) {
        INFO("table: " << t);
        REQUIRE(table_exists(db, t));
    }
}

TEST_CASE("migrate is idempotent", "[db][migration]") {
    Database db = Database::open_in_memory();
    REQUIRE(nexus::db::migrate(db, "core", nexus::db::core_migrations()) == 1);
    REQUIRE(nexus::db::migrate(db, "core", nexus::db::core_migrations()) == 1);

    auto stmt = db.prepare("SELECT COUNT(*) FROM schema_migrations WHERE component = 'core'");
    stmt.step();
    REQUIRE(stmt.column_int64(0) == 1);
}

TEST_CASE("migrate applies only pending versions in order", "[db][migration]") {
    Database db = Database::open_in_memory();

    const std::array<Migration, 2> first{{
        {1, "base", "CREATE TABLE a (id INTEGER)"},
        {2, "more", "CREATE TABLE b (id INTEGER)"},
    }};
    REQUIRE(nexus::db::migrate(db, "test", first) == 2);

    const std::array<Migration, 3> extended{{
        {1, "base", "CREATE TABLE a (id INTEGER)"},
        {2, "more", "CREATE TABLE b (id INTEGER)"},
        {3, "third", "CREATE TABLE c (id INTEGER)"},
    }};
    REQUIRE(nexus::db::migrate(db, "test", extended) == 3);
    REQUIRE(table_exists(db, "c"));
}

TEST_CASE("components keep independent version lines", "[db][migration]") {
    Database db = Database::open_in_memory();

    const std::array<Migration, 1> alpha{{{1, "alpha_one", "CREATE TABLE alpha (id INTEGER)"}}};
    const std::array<Migration, 2> beta{{
        {1, "beta_one", "CREATE TABLE beta1 (id INTEGER)"},
        {2, "beta_two", "CREATE TABLE beta2 (id INTEGER)"},
    }};

    REQUIRE(nexus::db::migrate(db, "alpha", alpha) == 1);
    REQUIRE(nexus::db::migrate(db, "beta", beta) == 2);
    REQUIRE(nexus::db::schema_version(db, "alpha") == 1);
    REQUIRE(nexus::db::schema_version(db, "beta") == 2);
    REQUIRE(nexus::db::schema_version(db, "unknown") == 0);
    REQUIRE(table_exists(db, "alpha"));
    REQUIRE(table_exists(db, "beta2"));
}

TEST_CASE("failed migration leaves earlier ones committed", "[db][migration]") {
    Database db = Database::open_in_memory();

    const std::array<Migration, 2> migrations{{
        {1, "ok", "CREATE TABLE a (id INTEGER)"},
        {2, "bad", "CREATE TABLE bad (syntax"},
    }};

    REQUIRE_THROWS(nexus::db::migrate(db, "test", migrations));
    REQUIRE(nexus::db::schema_version(db, "test") == 1);
    REQUIRE(table_exists(db, "a"));
    REQUIRE_FALSE(table_exists(db, "bad"));
}
