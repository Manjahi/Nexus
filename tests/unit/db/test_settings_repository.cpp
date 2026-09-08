#include "nexus/db/database.hpp"
#include "nexus/db/migration.hpp"
#include "nexus/db/settings_repository.hpp"

#include <catch2/catch_test_macros.hpp>

using nexus::db::Database;
using nexus::db::SettingsRepository;

namespace {

Database migrated_db() {
    Database db = Database::open_in_memory();
    nexus::db::migrate(db, "core", nexus::db::core_migrations());
    return db;
}

} // namespace

TEST_CASE("settings get/set/remove", "[db][settings]") {
    Database db = migrated_db();
    SettingsRepository settings(db);

    REQUIRE_FALSE(settings.get("theme").has_value());
    REQUIRE(settings.get_or("theme", "light") == "light");

    settings.set("theme", "dark");
    REQUIRE(settings.get("theme") == "dark");
    REQUIRE(settings.get_or("theme", "light") == "dark");

    settings.remove("theme");
    REQUIRE_FALSE(settings.get("theme").has_value());
}

TEST_CASE("settings set upserts on conflict", "[db][settings]") {
    Database db = migrated_db();
    SettingsRepository settings(db);

    settings.set("retention.days", "30");
    settings.set("retention.days", "90");
    REQUIRE(settings.get("retention.days") == "90");

    auto count = db.prepare("SELECT COUNT(*) FROM app_settings WHERE key = 'retention.days'");
    count.step();
    REQUIRE(count.column_int64(0) == 1);
}

TEST_CASE("settings all() returns rows sorted by key", "[db][settings]") {
    Database db = migrated_db();
    SettingsRepository settings(db);

    settings.set("b", "2");
    settings.set("a", "1");
    settings.set("c", "3");

    const auto rows = settings.all();
    REQUIRE(rows.size() == 3);
    REQUIRE(rows[0].first == "a");
    REQUIRE(rows[1].first == "b");
    REQUIRE(rows[2].first == "c");
    REQUIRE(rows[0].second == "1");
}
