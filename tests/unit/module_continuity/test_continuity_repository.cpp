#include "nexus/module/continuity/continuity_repository.hpp"

#include "nexus/db/database.hpp"
#include "nexus/db/migration.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

using namespace nexus::module::continuity;

namespace {
nexus::db::Database migrated_db() {
    auto db = nexus::db::Database::open_in_memory();
    nexus::db::migrate(db, "continuity", continuity_migrations());
    return db;
}
} // namespace

TEST_CASE("asset kind and criticality round-trip through their string forms",
         "[continuity][repo]") {
    REQUIRE(to_string(AssetKind::File) == "file");
    REQUIRE(to_string(AssetKind::Folder) == "folder");
    REQUIRE(to_string(AssetKind::App) == "app");
    REQUIRE(to_string(AssetKind::Credential) == "credential");
    REQUIRE(asset_kind_from_string("folder") == AssetKind::Folder);
    REQUIRE_FALSE(asset_kind_from_string("bogus").has_value());

    REQUIRE(to_string(Criticality::High) == "high");
    REQUIRE(criticality_from_string("low") == Criticality::Low);
    REQUIRE_FALSE(criticality_from_string("bogus").has_value());
}

TEST_CASE("tracked assets round-trip", "[continuity][repo]") {
    auto db = migrated_db();
    ContinuityRepository repo(db);

    TrackedAsset asset;
    asset.label = "Photos";
    asset.kind = AssetKind::Folder;
    asset.path = "C:/Users/me/Pictures";
    asset.criticality = Criticality::High;
    const auto id = repo.upsert_asset(asset);
    REQUIRE_FALSE(id.is_nil());

    const auto found = repo.find_asset(id);
    REQUIRE(found.has_value());
    CHECK(found->label == "Photos");
    CHECK(found->kind == AssetKind::Folder);
    CHECK(found->path == "C:/Users/me/Pictures");
    CHECK(found->criticality == Criticality::High);

    const auto all = repo.list_assets();
    REQUIRE(all.size() == 1);

    // Update via upsert (same id).
    TrackedAsset updated = *found;
    updated.criticality = Criticality::Low;
    repo.upsert_asset(updated);
    REQUIRE(repo.find_asset(id)->criticality == Criticality::Low);
    REQUIRE(repo.list_assets().size() == 1); // still one row, not a duplicate

    REQUIRE(repo.remove_asset(id));
    REQUIRE_FALSE(repo.remove_asset(id)); // already gone
    REQUIRE(repo.list_assets().empty());
}

TEST_CASE("a credential asset carries a vault entry id instead of a path",
         "[continuity][repo]") {
    auto db = migrated_db();
    ContinuityRepository repo(db);

    TrackedAsset asset;
    asset.label = "Email password";
    asset.kind = AssetKind::Credential;
    asset.vault_entry_id = "some-vault-entry-id";
    const auto id = repo.upsert_asset(asset);

    const auto found = repo.find_asset(id);
    REQUIRE(found.has_value());
    CHECK(found->kind == AssetKind::Credential);
    CHECK(found->vault_entry_id == "some-vault-entry-id");
    CHECK(found->path.empty());
}

TEST_CASE("rehearsals begin running and finish with an outcome", "[continuity][repo]") {
    auto db = migrated_db();
    ContinuityRepository repo(db);

    REQUIRE_FALSE(repo.latest_rehearsal().has_value());

    const auto id = repo.begin_rehearsal("Quick Rehearsal");
    auto running = repo.latest_rehearsal();
    REQUIRE(running.has_value());
    CHECK(running->scenario == "Quick Rehearsal");
    CHECK(running->outcome == "running");
    CHECK_FALSE(running->finished_at.has_value());

    repo.finish_rehearsal(id, "success", 42, 1024, "restored 42 files cleanly");
    const auto finished = repo.latest_rehearsal();
    REQUIRE(finished.has_value());
    CHECK(finished->outcome == "success");
    CHECK(finished->files_restored == 42);
    CHECK(finished->bytes_restored == 1024);
    CHECK(finished->detail == "restored 42 files cleanly");
    CHECK(finished->finished_at.has_value());
}

TEST_CASE("recent_rehearsals returns newest first, capped at the limit",
         "[continuity][repo]") {
    auto db = migrated_db();
    ContinuityRepository repo(db);

    for (int i = 0; i < 3; ++i) {
        const auto id = repo.begin_rehearsal("Disk Failure");
        repo.finish_rehearsal(id, "success", 1, 1, "");
    }

    const auto recent = repo.recent_rehearsals(2);
    REQUIRE(recent.size() == 2);
}

TEST_CASE("capsule export timestamp is unset until recorded", "[continuity][repo]") {
    auto db = migrated_db();
    ContinuityRepository repo(db);

    REQUIRE_FALSE(repo.latest_capsule_export().has_value());

    const auto now = nexus::core::now();
    repo.record_capsule_export(now);
    const auto exported = repo.latest_capsule_export();
    REQUIRE(exported.has_value());

    // ISO 8601 round-trip is second-precision - compare at that granularity.
    const auto diff = *exported > now ? *exported - now : now - *exported;
    CHECK(diff < std::chrono::seconds{1});

    // Recording again replaces the single row rather than adding a second one.
    const auto later = now + std::chrono::hours{1};
    repo.record_capsule_export(later);
    const auto diff2 = *repo.latest_capsule_export() > later ? *repo.latest_capsule_export() - later
                                                              : later - *repo.latest_capsule_export();
    CHECK(diff2 < std::chrono::seconds{1});
}
