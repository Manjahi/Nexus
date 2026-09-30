#include "nexus/db/database.hpp"
#include "nexus/db/migration.hpp"
#include "nexus/fs/exclusion_rules.hpp"
#include "nexus/module/storage/duplicate_scanner.hpp"
#include "nexus/module/storage/storage_repository.hpp"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

namespace fs = std::filesystem;
using nexus::module::storage::DuplicateScanner;
using nexus::module::storage::ScanOptions;

namespace {

struct DupTree {
    fs::path root;
    DupTree() {
        const auto tag = std::chrono::steady_clock::now().time_since_epoch().count();
        root = fs::temp_directory_path() / ("nexuspc_dup_" + std::to_string(tag));
        fs::create_directories(root / "d");
        write(root / "a.txt", "hello world");
        write(root / "b.txt", "hello world");
        write(root / "d" / "e.txt", "hello world");
        write(root / "c.txt", "something else entirely");
        write(root / "x.txt", "AAAAA"); // same size as y, different content
        write(root / "y.txt", "BBBBB");
        write(root / "big1.bin", std::string(40000, 'Z'));
        write(root / "big2.bin", std::string(40000, 'Z')); // large duplicate pair
    }
    ~DupTree() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    static void write(const fs::path& p, std::string_view content) {
        std::ofstream out(p, std::ios::binary);
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
    }
};

nexus::db::Database migrated_db() {
    auto db = nexus::db::Database::open_in_memory();
    nexus::db::migrate(db, "storage", nexus::module::storage::storage_migrations());
    return db;
}

} // namespace

TEST_CASE("scanner finds exact duplicates and computes reclaimable bytes", "[storage][scanner]") {
    DupTree tree;
    DuplicateScanner scanner; // no persistence
    ScanOptions opts;
    opts.persist = false;

    const auto summary = scanner.scan(tree.root, nexus::fs::ExclusionRules{}, opts);

    REQUIRE_FALSE(summary.cancelled);
    REQUIRE(summary.files_seen == 8);
    REQUIRE(summary.groups.size() == 2);

    // Groups are ordered by reclaimable bytes, so the 40 KB pair comes first.
    REQUIRE(summary.groups[0].files.size() == 2);
    REQUIRE(summary.groups[0].file_size == 40000);
    REQUIRE(summary.groups[0].reclaimable_bytes() == 40000);

    REQUIRE(summary.groups[1].files.size() == 3); // a, b, d/e
    REQUIRE(summary.groups[1].file_size == 11);
    REQUIRE(summary.groups[1].reclaimable_bytes() == 22);

    REQUIRE(summary.reclaimable_bytes() == 40022);
    REQUIRE(summary.duplicate_file_count() == 5);
}

TEST_CASE("min_file_size skips small files", "[storage][scanner]") {
    DupTree tree;
    DuplicateScanner scanner;
    ScanOptions opts;
    opts.persist = false;
    opts.min_file_size = 1000;

    const auto summary = scanner.scan(tree.root, nexus::fs::ExclusionRules{}, opts);
    REQUIRE(summary.groups.size() == 1); // only the 40 KB pair
    REQUIRE(summary.groups[0].file_size == 40000);
}

TEST_CASE("exclusions keep files out of the scan", "[storage][scanner]") {
    DupTree tree;
    DuplicateScanner scanner;
    ScanOptions opts;
    opts.persist = false;

    nexus::fs::ExclusionRules rules;
    rules.exclude_directory_name("d");

    const auto summary = scanner.scan(tree.root, rules, opts);
    // a.txt + b.txt remain a pair (d/e.txt excluded); plus the big pair.
    REQUIRE(summary.groups.size() == 2);
    for (const auto& g : summary.groups) {
        if (g.file_size == 11) {
            REQUIRE(g.files.size() == 2);
        }
    }
}

TEST_CASE("scan persists to the storage tables", "[storage][scanner]") {
    DupTree tree;
    auto db = migrated_db();
    nexus::module::storage::StorageRepository repo(db);
    DuplicateScanner scanner(&repo);

    const auto summary = scanner.scan(tree.root, nexus::fs::ExclusionRules{});

    const auto scan = repo.latest_scan();
    REQUIRE(scan.has_value());
    REQUIRE(scan->state == "completed");
    REQUIRE(scan->duplicate_groups == 2);
    REQUIRE(scan->reclaimable_bytes == 40022);
    REQUIRE(scan->id == summary.scan_id);

    const auto groups = repo.groups_for(scan->id);
    REQUIRE(groups.size() == 2);
    REQUIRE(groups[0].reclaimable_bytes == 40000);
    REQUIRE(repo.files_in_group(groups[1].id).size() == 3);
}

TEST_CASE("cancellation during the walk stops the scan", "[storage][scanner]") {
    DupTree tree;
    DuplicateScanner scanner;
    ScanOptions opts;
    opts.persist = false;

    const auto summary =
        scanner.scan(tree.root, nexus::fs::ExclusionRules{}, opts, {}, [] { return true; });
    REQUIRE(summary.cancelled);
    REQUIRE(summary.groups.empty());
}
