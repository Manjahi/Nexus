#include "nexus/db/database.hpp"
#include "nexus/db/migration.hpp"
#include "nexus/db/settings_repository.hpp"
#include "nexus/services/module_registry.hpp"

#include <catch2/catch_test_macros.hpp>
#include <vector>

using nexus::services::ModuleInfo;
using nexus::services::ModuleRegistry;

namespace {

nexus::db::Database migrated_db() {
    auto db = nexus::db::Database::open_in_memory();
    nexus::db::migrate(db, "core", nexus::db::core_migrations());
    return db;
}

std::vector<ModuleInfo> sample_modules() {
    return {
        {"storage", "Storage Intelligence", true},
        {"vault", "Secure Vault", false},
    };
}

} // namespace

TEST_CASE("defaults apply until overridden", "[services][modules]") {
    auto db = migrated_db();
    nexus::db::SettingsRepository settings(db);
    ModuleRegistry registry(settings, sample_modules());

    REQUIRE(registry.is_known("storage"));
    REQUIRE_FALSE(registry.is_known("unknown"));
    REQUIRE(registry.is_enabled("storage"));
    REQUIRE_FALSE(registry.is_enabled("vault"));
    REQUIRE_FALSE(registry.is_enabled("unknown"));
}

TEST_CASE("set_enabled persists and is visible to a fresh registry", "[services][modules]") {
    auto db = migrated_db();
    nexus::db::SettingsRepository settings(db);

    {
        ModuleRegistry registry(settings, sample_modules());
        REQUIRE(registry.set_enabled("vault", true));
        REQUIRE(registry.is_enabled("vault"));
        REQUIRE(registry.set_enabled("storage", false));
    }

    ModuleRegistry reloaded(settings, sample_modules());
    REQUIRE(reloaded.is_enabled("vault"));
    REQUIRE_FALSE(reloaded.is_enabled("storage"));
}

TEST_CASE("set_enabled is a no-op for unknown ids or unchanged state", "[services][modules]") {
    auto db = migrated_db();
    nexus::db::SettingsRepository settings(db);
    ModuleRegistry registry(settings, sample_modules());

    REQUIRE_FALSE(registry.set_enabled("unknown", true));
    REQUIRE_FALSE(registry.set_enabled("storage", true)); // already enabled by default
    REQUIRE(registry.set_enabled("storage", false));
    REQUIRE_FALSE(registry.set_enabled("storage", false));
}

TEST_CASE("modules() preserves the supplied order", "[services][modules]") {
    auto db = migrated_db();
    nexus::db::SettingsRepository settings(db);
    ModuleRegistry registry(settings, sample_modules());

    const auto& mods = registry.modules();
    REQUIRE(mods.size() == 2);
    REQUIRE(mods[0].id == "storage");
    REQUIRE(mods[1].display_name == "Secure Vault");
}
