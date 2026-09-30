#include "nexus/db/database.hpp"
#include "nexus/db/migration.hpp"
#include "nexus/db/settings_repository.hpp"
#include "nexus/services/retention_setting.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace nexus::services;

namespace {

nexus::db::Database migrated_db() {
    auto db = nexus::db::Database::open_in_memory();
    nexus::db::migrate(db, "core", nexus::db::core_migrations());
    return db;
}

} // namespace

TEST_CASE("retention_days_setting falls back to the default when unset", "[services][retention]") {
    auto db = migrated_db();
    nexus::db::SettingsRepository settings(db);

    REQUIRE(retention_days_setting(settings, "retention.hardware.days", 7) ==
            std::chrono::hours{24 * 7});
}

TEST_CASE("retention_days_setting reads a configured value", "[services][retention]") {
    auto db = migrated_db();
    nexus::db::SettingsRepository settings(db);
    settings.set("retention.hardware.days", "3");

    REQUIRE(retention_days_setting(settings, "retention.hardware.days", 7) ==
            std::chrono::hours{24 * 3});
}

TEST_CASE("retention_days_setting falls back on an unparsable value", "[services][retention]") {
    auto db = migrated_db();
    nexus::db::SettingsRepository settings(db);
    settings.set("retention.hardware.days", "not-a-number");

    REQUIRE(retention_days_setting(settings, "retention.hardware.days", 7) ==
            std::chrono::hours{24 * 7});
}

TEST_CASE("retention_days_setting clamps below-1 values up to one day", "[services][retention]") {
    auto db = migrated_db();
    nexus::db::SettingsRepository settings(db);
    settings.set("retention.hardware.days", "0");

    REQUIRE(retention_days_setting(settings, "retention.hardware.days", 7) ==
            std::chrono::hours{24});

    settings.set("retention.hardware.days", "-5");
    REQUIRE(retention_days_setting(settings, "retention.hardware.days", 7) ==
            std::chrono::hours{24});
}

TEST_CASE("retention_days_setting reads independent keys independently", "[services][retention]") {
    auto db = migrated_db();
    nexus::db::SettingsRepository settings(db);
    settings.set("retention.connectivity.days", "14");

    REQUIRE(retention_days_setting(settings, "retention.hardware.days", 7) ==
            std::chrono::hours{24 * 7});
    REQUIRE(retention_days_setting(settings, "retention.connectivity.days", 30) ==
            std::chrono::hours{24 * 14});
}
