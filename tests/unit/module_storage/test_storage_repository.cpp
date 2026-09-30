#include "nexus/db/database.hpp"
#include "nexus/db/migration.hpp"
#include "nexus/module/storage/storage_repository.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace nexus::module::storage;

namespace {

nexus::db::Database migrated_db() {
    auto db = nexus::db::Database::open_in_memory();
    nexus::db::migrate(db, "storage", storage_migrations());
    return db;
}

DuplicateGroup group_of(std::string digest, std::uint64_t size,
                        std::vector<std::filesystem::path> files) {
    DuplicateGroup g;
    g.digest = std::move(digest);
    g.file_size = size;
    g.files = std::move(files);
    return g;
}

} // namespace

TEST_CASE("begin_scan / add_group / finish_scan round-trip", "[storage][repo]") {
    auto db = migrated_db();
    StorageRepository repo(db);

    const auto scan_id = repo.begin_scan("C:/data");
    REQUIRE(repo.latest_scan()->state == "running");

    repo.add_group(scan_id, group_of("aa", 100, {"C:/data/a", "C:/data/b", "C:/data/c"}));
    repo.add_group(scan_id, group_of("bb", 10, {"C:/data/x", "C:/data/y"}));

    ScanSummary summary;
    summary.scan_id = scan_id;
    summary.files_seen = 42;
    summary.bytes_seen = 9999;
    summary.groups = {group_of("aa", 100, {"a", "b", "c"}), group_of("bb", 10, {"x", "y"})};
    repo.finish_scan(scan_id, summary);

    const auto scan = repo.latest_scan();
    REQUIRE(scan->state == "completed");
    REQUIRE(scan->files_seen == 42);
    REQUIRE(scan->duplicate_groups == 2);
    REQUIRE(scan->reclaimable_bytes == 100 * 2 + 10 * 1);
    REQUIRE(scan->finished_at.has_value());

    const auto groups = repo.groups_for(scan_id);
    REQUIRE(groups.size() == 2);
    REQUIRE(groups[0].reclaimable_bytes == 200); // ordered by reclaimable desc
    REQUIRE(groups[0].file_count == 3);
    REQUIRE(repo.files_in_group(groups[0].id) ==
            std::vector<std::string>{"C:/data/a", "C:/data/b", "C:/data/c"});
}

TEST_CASE("deleting a scan cascades to its groups and files", "[storage][repo]") {
    auto db = migrated_db();
    StorageRepository repo(db);

    const auto drop = repo.begin_scan("C:/drop");
    const auto keep = repo.begin_scan("C:/keep"); // newer -> survives prune(keep=1)
    repo.add_group(drop, group_of("dd", 5, {"1", "2"}));
    repo.add_group(keep, group_of("kk", 5, {"3", "4"}));

    REQUIRE(repo.prune_scans_keeping(1) == 1);

    const auto scans = repo.scans();
    REQUIRE(scans.size() == 1);
    REQUIRE(scans[0].root == "C:/keep");
    REQUIRE(repo.groups_for(drop).empty());
    REQUIRE(repo.groups_for(keep).size() == 1);
}
