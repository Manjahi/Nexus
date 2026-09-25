#include "nexus/module/backup/backup_engine.hpp"
#include "nexus/module/backup/backup_module.hpp"
#include "nexus/module/backup/backup_repository.hpp"
#include "nexus/module/backup/object_store.hpp"
#include "nexus/module/backup/restore_engine.hpp"

#include "nexus/db/database.hpp"
#include "nexus/db/migration.hpp"
#include "nexus/fs/exclusion_rules.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>

namespace fs = std::filesystem;
using namespace nexus::module::backup;

namespace {

struct Fixture {
    fs::path base;
    fs::path source;
    fs::path store_root;

    Fixture() {
        const auto tag = std::chrono::steady_clock::now().time_since_epoch().count();
        base = fs::temp_directory_path() / ("nexuspc_backup_" + std::to_string(tag));
        source = base / "src";
        store_root = base / "dest" / "objects";
        fs::create_directories(source / "sub");
        write(source / "one.txt", "file one");
        write(source / "two.txt", "file two");
        write(source / "sub" / "three.txt", "file one"); // duplicate content of one.txt
    }
    ~Fixture() {
        std::error_code ec;
        fs::remove_all(base, ec);
    }
    static void write(const fs::path& p, std::string_view content) {
        std::ofstream out(p, std::ios::binary);
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
    }
    static std::string read(const fs::path& p) {
        std::ifstream in(p, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
    }
};

nexus::db::Database migrated_db() {
    auto db = nexus::db::Database::open_in_memory();
    nexus::db::migrate(db, "backup", backup_migrations());
    return db;
}

} // namespace

TEST_CASE("a snapshot stores each unique blob once", "[backup][engine]") {
    Fixture f;
    auto db = migrated_db();
    BackupRepository repo(db);
    ObjectStore store(f.store_root);
    BackupEngine engine(store, &repo);

    BackupJob job;
    job.name = "docs";
    job.source_root = f.source.generic_string();
    job.destination = (f.base / "dest").generic_string();
    const auto job_id = repo.upsert_job(job);

    const auto summary = engine.run(job_id, f.source, nexus::fs::ExclusionRules{});

    REQUIRE_FALSE(summary.cancelled);
    REQUIRE(summary.file_count == 3);
    REQUIRE(summary.total_bytes == 8 + 8 + 8);
    REQUIRE(summary.new_bytes == 8 + 8); // "file one" written once, reused for three.txt

    const auto snap = repo.latest_snapshot(job_id);
    REQUIRE(snap.has_value());
    REQUIRE(snap->state == "completed");
    REQUIRE(repo.files_in(snap->id).size() == 3);
}

TEST_CASE("a second snapshot only stores changed content", "[backup][engine]") {
    Fixture f;
    auto db = migrated_db();
    BackupRepository repo(db);
    ObjectStore store(f.store_root);
    BackupEngine engine(store, &repo);

    BackupJob job;
    job.source_root = f.source.generic_string();
    job.destination = (f.base / "dest").generic_string();
    const auto job_id = repo.upsert_job(job);

    engine.run(job_id, f.source, nexus::fs::ExclusionRules{});

    const std::string revised = "file two, revised and much longer than before";
    Fixture::write(f.source / "two.txt", revised);
    const auto second = engine.run(job_id, f.source, nexus::fs::ExclusionRules{});

    REQUIRE(second.file_count == 3);
    REQUIRE(second.new_bytes == revised.size()); // only the rewritten two.txt
    REQUIRE(repo.snapshots_for(job_id).size() == 2);
}

TEST_CASE("verify then restore a snapshot", "[backup][engine]") {
    Fixture f;
    auto db = migrated_db();
    BackupRepository repo(db);
    ObjectStore store(f.store_root);
    BackupEngine engine(store, &repo);

    BackupJob job;
    job.source_root = f.source.generic_string();
    job.destination = (f.base / "dest").generic_string();
    const auto job_id = repo.upsert_job(job);
    const auto summary = engine.run(job_id, f.source, nexus::fs::ExclusionRules{});

    const auto verify = engine.verify(summary.snapshot_id);
    REQUIRE(verify.checked == 3);
    REQUIRE(verify.healthy());

    REQUIRE_FALSE(repo.latest_snapshot(job_id)->verified_at.has_value());
    repo.mark_verified(summary.snapshot_id, nexus::core::now());
    REQUIRE(repo.latest_snapshot(job_id)->verified_at.has_value());

    RestoreEngine restorer(store, repo);
    const fs::path target = f.base / "restored";
    const auto restore = restorer.restore(summary.snapshot_id, target);

    REQUIRE(restore.ok());
    REQUIRE(restore.files_restored == 3);
    REQUIRE(Fixture::read(target / "one.txt") == "file one");
    REQUIRE(Fixture::read(target / "sub" / "three.txt") == "file one");
}

TEST_CASE("restore of a single file", "[backup][engine]") {
    Fixture f;
    auto db = migrated_db();
    BackupRepository repo(db);
    ObjectStore store(f.store_root);
    BackupEngine engine(store, &repo);

    BackupJob job;
    job.source_root = f.source.generic_string();
    job.destination = (f.base / "dest").generic_string();
    const auto job_id = repo.upsert_job(job);
    const auto summary = engine.run(job_id, f.source, nexus::fs::ExclusionRules{});

    RestoreEngine restorer(store, repo);
    const fs::path target = f.base / "one_only";
    const auto restore = restorer.restore(summary.snapshot_id, target, "two.txt");

    REQUIRE(restore.files_restored == 1);
    REQUIRE(fs::exists(target / "two.txt"));
    REQUIRE_FALSE(fs::exists(target / "one.txt"));
}

TEST_CASE("parse_schedule reads simple intervals", "[backup][schedule]") {
    using namespace std::chrono;
    REQUIRE(parse_schedule("every 30s") == seconds{30});
    REQUIRE(parse_schedule("every 15m") == minutes{15});
    REQUIRE(parse_schedule("every 6h") == hours{6});
    REQUIRE(parse_schedule("EVERY 1D") == hours{24});
    REQUIRE(parse_schedule("  2h ") == hours{2}); // "every" prefix optional

    REQUIRE_FALSE(parse_schedule("").has_value());
    REQUIRE_FALSE(parse_schedule("sometimes").has_value());
    REQUIRE_FALSE(parse_schedule("every 0h").has_value());
    REQUIRE_FALSE(parse_schedule("every -3h").has_value());
    REQUIRE_FALSE(parse_schedule("every 5x").has_value());
}

TEST_CASE("prune keeps the newest snapshots", "[backup][engine]") {
    Fixture f;
    auto db = migrated_db();
    BackupRepository repo(db);
    ObjectStore store(f.store_root);
    BackupEngine engine(store, &repo);

    BackupJob job;
    job.source_root = f.source.generic_string();
    job.destination = (f.base / "dest").generic_string();
    const auto job_id = repo.upsert_job(job);

    for (int i = 0; i < 4; ++i) {
        engine.run(job_id, f.source, nexus::fs::ExclusionRules{});
    }
    REQUIRE(repo.snapshots_for(job_id).size() == 4);

    const auto removed = repo.prune_snapshots(job_id, 2);
    REQUIRE(removed.size() == 2);
    REQUIRE(repo.snapshots_for(job_id).size() == 2);
}

TEST_CASE("pruning then collecting garbage reclaims blobs no snapshot needs anymore",
         "[backup][engine]") {
    Fixture f;
    auto db = migrated_db();
    BackupRepository repo(db);
    ObjectStore store(f.store_root);
    BackupEngine engine(store, &repo);

    BackupJob job;
    job.source_root = f.source.generic_string();
    job.destination = (f.base / "dest").generic_string();
    const auto job_id = repo.upsert_job(job);

    // First snapshot's content ("file one"/"file two") is unique to it -
    // once it's pruned, nothing should reference those blobs anymore.
    engine.run(job_id, f.source, nexus::fs::ExclusionRules{});
    Fixture::write(f.source / "one.txt", "completely different content for round two");
    Fixture::write(f.source / "two.txt", "also completely different for round two");
    engine.run(job_id, f.source, nexus::fs::ExclusionRules{});

    // sub/three.txt was never rewritten, so both snapshots reference the same blob.
    const auto surviving_digest = nexus::hash::hash_file(f.source / "sub" / "three.txt");
    REQUIRE(surviving_digest.has_value());
    REQUIRE(store.contains(*surviving_digest));

    repo.prune_snapshots(job_id, 1); // drop the first snapshot, keep only round two
    const auto referenced = repo.all_referenced_digests();
    const auto gc = store.collect_garbage(
        std::unordered_set<std::string>(referenced.begin(), referenced.end()));

    REQUIRE(gc.blobs_removed > 0); // round one's unique "file one"/"file two" blobs
    // sub/three.txt's content never changed, so its blob must survive.
    REQUIRE(store.contains(*surviving_digest));
}
