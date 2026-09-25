// Continuity's B4: "Run Quick Rehearsal" must be a REAL restore-to-scratch
// plus a real verify, not a simulated/estimated result. This seeds an actual
// BackupEngine snapshot over real files, runs run_quick_rehearsal() against
// it, and asserts both the continuity_rehearsals row AND the bytes that
// actually landed on disk in the scratch directory - the same shape as
// tests/integration/test_backup_schedule_restart.cpp's "exercise the real
// thing, not a mock of it" approach, just without needing a full
// ServiceContext/ModuleHost (run_quick_rehearsal() only needs a Database).

#include "nexus/module/backup/backup_engine.hpp"
#include "nexus/module/backup/backup_repository.hpp"
#include "nexus/module/backup/object_store.hpp"
#include "nexus/module/continuity/continuity_rehearsal.hpp"
#include "nexus/module/continuity/continuity_repository.hpp"

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

namespace fs = std::filesystem;
using namespace nexus::module::continuity;
using namespace nexus::module::backup;

namespace {

struct Fixture {
    fs::path base;
    fs::path source;
    fs::path store_root;
    fs::path scratch;

    Fixture() {
        const auto tag = std::chrono::steady_clock::now().time_since_epoch().count();
        base = fs::temp_directory_path() / ("nexuspc_rehearsal_" + std::to_string(tag));
        source = base / "src";
        store_root = base / "dest" / "objects";
        scratch = base / "scratch";
        fs::create_directories(source);
        write(source / "important.txt", "the actual data that must survive");
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
    nexus::db::migrate(db, "continuity", continuity_migrations());
    return db;
}

} // namespace

TEST_CASE("a quick rehearsal with no backup jobs fails honestly", "[continuity][rehearsal]") {
    Fixture f;
    auto db = migrated_db();

    const auto result = run_quick_rehearsal(db, f.scratch);
    CHECK(result.outcome == "failed");
    CHECK(result.files_restored == 0);

    ContinuityRepository continuity(db);
    const auto row = continuity.recent_rehearsals(1);
    REQUIRE(row.size() == 1);
    CHECK(row.front().outcome == "failed");
    CHECK(row.front().finished_at.has_value());
}

TEST_CASE("a quick rehearsal really restores the most recent snapshot and verifies it",
         "[continuity][rehearsal]") {
    Fixture f;
    auto db = migrated_db();
    BackupRepository backup(db);
    ObjectStore store(f.store_root);
    BackupEngine engine(store, &backup);

    BackupJob job;
    job.source_root = f.source.generic_string();
    job.destination = (f.base / "dest").generic_string();
    const auto job_id = backup.upsert_job(job);
    const auto summary = engine.run(job_id, f.source, nexus::fs::ExclusionRules{});
    REQUIRE(summary.errors == 0);
    REQUIRE_FALSE(summary.cancelled);

    // Not verified yet - a rehearsal should establish that itself.
    REQUIRE_FALSE(backup.latest_snapshot(job_id)->verified_at.has_value());

    const auto result = run_quick_rehearsal(db, f.scratch);
    CHECK(result.outcome == "success");
    CHECK(result.files_restored == 1);

    // The actual bytes really landed in the scratch directory.
    REQUIRE(fs::exists(f.scratch / "important.txt"));
    CHECK(Fixture::read(f.scratch / "important.txt") == "the actual data that must survive");

    // A clean rehearsal counts as a verify - Disk Failure readiness benefits.
    CHECK(backup.latest_snapshot(job_id)->verified_at.has_value());

    ContinuityRepository continuity(db);
    const auto rehearsals = continuity.recent_rehearsals(1);
    REQUIRE(rehearsals.size() == 1);
    CHECK(rehearsals.front().scenario == "Quick Rehearsal");
    CHECK(rehearsals.front().outcome == "success");
    CHECK(rehearsals.front().files_restored == 1);
    CHECK(rehearsals.front().finished_at.has_value());
}
