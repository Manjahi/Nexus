// UFR-014 (scheduled jobs survive application restart): BackupModule::start()
// reads each persisted job's `schedule` string and re-registers it with
// ctx.scheduler on every launch. The mechanism (parse_schedule, and the
// job-repository round-trip) is unit-tested, but nothing previously exercised
// a REAL restart: two independent ServiceContext/ModuleHost instances, built
// one after the other against the SAME on-disk database file, the way
// apps/desktop/src/Platform is torn down and rebuilt across an app relaunch.
// See docs/UFR_CONFORMANCE.md's "Known gaps" for the gap this closes.

#include "nexus/db/database.hpp"
#include "nexus/db/migration.hpp"
#include "nexus/db/settings_repository.hpp"
#include "nexus/jobs/scheduler.hpp"
#include "nexus/jobs/thread_pool.hpp"
#include "nexus/module/backup/backup_module.hpp"
#include "nexus/module/backup/backup_repository.hpp"
#include "nexus/notify/notification_center.hpp"
#include "nexus/services/audit_log.hpp"
#include "nexus/services/event_bus.hpp"
#include "nexus/services/heavy_job_guard.hpp"
#include "nexus/services/job_repository.hpp"
#include "nexus/services/module_host.hpp"
#include "nexus/services/module_registry.hpp"
#include "nexus/services/notification_repository.hpp"
#include "nexus/services/report_center.hpp"
#include "nexus/services/service_context.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace nexus;
namespace fs = std::filesystem;

namespace {

// Same shape as tests/integration/test_platform_bringup.cpp's Harness, but
// opens a real on-disk Database at a caller-supplied path instead of an
// in-memory one, so a second instance can be built against the same file to
// simulate a restart.
struct RestartableHarness {
    fs::path reports_dir;
    db::Database db;
    db::SettingsRepository settings{db};
    jobs::ThreadPool pool{2};
    jobs::Scheduler scheduler{pool};
    notify::NotificationCenter notifications;
    services::EventBus events;
    services::AuditLog audit{db};
    services::ModuleRegistry modules;
    services::JobRepository jobs_repo{db};
    services::NotificationRepository notifications_repo{db};
    services::ReportCenter reports;
    services::HeavyJobGuard heavy_jobs;
    services::ServiceContext ctx;
    services::ModuleHost module_host;

    explicit RestartableHarness(const fs::path& db_path, fs::path reports_dir_)
        : reports_dir(std::move(reports_dir_)), db(db::Database::open(db_path)),
          modules(settings, {{"backup", "Backup & Recovery", true}}), reports(db, reports_dir),
          ctx{db,    settings, pool,      scheduler,          notifications, events,
              audit, modules,  jobs_repo, notifications_repo, reports,       heavy_jobs},
          module_host(ctx) {
        db::migrate(db, "core", db::core_migrations());
        db::migrate(db, "backup", module::backup::backup_migrations());
        std::filesystem::create_directories(reports_dir);
        services::attach_persistence(notifications, notifications_repo);
    }

    ~RestartableHarness() {
        module_host.stop_all();
        scheduler.stop();
        // See Platform.cpp / test_platform_bringup.cpp's Harness: drain the
        // pool before the members below it are destroyed.
        pool.wait_idle();
        std::error_code ec;
        std::filesystem::remove_all(reports_dir, ec);
    }
};

fs::path make_scratch_dir(std::string_view tag) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return fs::temp_directory_path() /
           ("nexuspc_backup_restart_" + std::string(tag) + "_" + std::to_string(stamp));
}

} // namespace

TEST_CASE("a scheduled backup job re-arms after a real restart against the same database",
          "[integration][backup][schedule]") {
    const fs::path base = make_scratch_dir("base");
    const fs::path db_path = base / "nexus.db";
    const fs::path source = base / "src";
    const fs::path destination = base / "dest";
    fs::create_directories(source);
    {
        std::ofstream out(source / "file.txt", std::ios::binary);
        out << "hello from the restart test";
    }

    core::Uuid job_id;
    {
        // Seed the job directly, the way the desktop UI's "add job" flow
        // would, before any Platform-equivalent harness exists yet.
        auto db = db::Database::open(db_path);
        db::migrate(db, "core", db::core_migrations());
        db::migrate(db, "backup", module::backup::backup_migrations());

        module::backup::BackupRepository repo(db);
        module::backup::BackupJob job;
        job.name = "restart-test";
        job.source_root = source.string();
        job.destination = destination.string();
        job.schedule = "every1s"; // fast enough to observe within a short real wait
        job.retention_keep = 5;
        job.enabled = true;
        job_id = repo.upsert_job(job);
    }

    std::size_t snapshots_after_first_run = 0;
    {
        RestartableHarness h1(db_path, make_scratch_dir("reports1"));
        h1.module_host.add(std::make_unique<module::backup::BackupModule>());
        REQUIRE(h1.module_host.failures().empty());
        h1.module_host.start_enabled();
        REQUIRE(h1.module_host.is_running("backup"));

        module::backup::BackupRepository repo(h1.db);
        bool ran = false;
        for (int i = 0; i < 50 && !ran; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds{200});
            ran = repo.latest_snapshot(job_id).has_value();
        }
        REQUIRE(ran);
        snapshots_after_first_run = repo.snapshots_for(job_id).size();
        REQUIRE(snapshots_after_first_run >= 1);
    } // h1 destructs: schedule cancelled, pool drained - nothing left running.

    {
        // A fresh harness against the SAME on-disk database, standing in for
        // an application relaunch. BackupModule::start() must read the
        // persisted job row's schedule and re-register it on its own -
        // nothing here re-inserts the job or re-arms anything by hand.
        RestartableHarness h2(db_path, make_scratch_dir("reports2"));
        h2.module_host.add(std::make_unique<module::backup::BackupModule>());
        REQUIRE(h2.module_host.failures().empty());
        h2.module_host.start_enabled();
        REQUIRE(h2.module_host.is_running("backup"));

        module::backup::BackupRepository repo(h2.db);
        bool grew = false;
        for (int i = 0; i < 50 && !grew; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds{200});
            grew = repo.snapshots_for(job_id).size() > snapshots_after_first_run;
        }
        REQUIRE(grew);
    }

    std::error_code ec;
    fs::remove_all(base, ec);
}

// Previously, a job created (or enabled, or schedule-edited) after
// BackupModule::start() had already run required an app restart to ever
// fire, because schedule_ids_/scheduled_ were only populated once, at
// start(). BackupModule::reschedule_job() closes that: the desktop UI now
// calls it right after creating a job (MainWindow::newBackupJob()).
TEST_CASE("a job created after start() is scheduled via reschedule_job() without a restart",
          "[integration][backup][schedule]") {
    const fs::path base = make_scratch_dir("live");
    const fs::path db_path = base / "nexus.db";
    const fs::path source = base / "src";
    const fs::path destination = base / "dest";
    fs::create_directories(source);
    {
        std::ofstream out(source / "file.txt", std::ios::binary);
        out << "hello from the live-reschedule test";
    }

    RestartableHarness h(db_path, make_scratch_dir("reports"));
    auto owned = std::make_unique<module::backup::BackupModule>();
    module::backup::BackupModule* backup_module = owned.get();
    h.module_host.add(std::move(owned));
    REQUIRE(h.module_host.failures().empty());
    h.module_host.start_enabled(); // no jobs exist yet - nothing scheduled

    module::backup::BackupRepository repo(h.db);
    module::backup::BackupJob job;
    job.name = "live-reschedule-test";
    job.source_root = source.string();
    job.destination = destination.string();
    job.schedule = "every1s";
    job.retention_keep = 5;
    job.enabled = true;
    const auto job_id = repo.upsert_job(job);

    // Without this call, the job above would never run until the process
    // restarted - that's exactly the gap this test guards against.
    backup_module->reschedule_job(job_id);

    bool ran = false;
    for (int i = 0; i < 50 && !ran; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds{200});
        ran = repo.latest_snapshot(job_id).has_value();
    }
    REQUIRE(ran);

    std::error_code ec;
    fs::remove_all(base, ec);
}

// Spec section 9 hook #5 (Search->Backup "is this in my latest backup?"):
// BackupEngine stores each snapshot file's path RELATIVE to its job's
// source_root (see backup_engine.cpp), while Search indexes absolute
// filesystem paths - BackupModule::is_path_backed_up() has to bridge that,
// per-job, rather than doing a flat string match. Seeds a snapshot directly
// (bypassing BackupEngine's real directory scan) since only the repository
// shape matters here, not the scan itself.
TEST_CASE("is_path_backed_up matches an absolute path against its job's "
          "source-root-relative snapshot entries",
          "[integration][backup][search]") {
    const fs::path base = make_scratch_dir("search_hook");
    const fs::path db_path = base / "nexus.db";
    const fs::path source = base / "src";
    const fs::path destination = base / "dest";
    fs::create_directories(source / "sub");

    core::Uuid job_id;
    {
        auto db = db::Database::open(db_path);
        db::migrate(db, "core", db::core_migrations());
        db::migrate(db, "backup", module::backup::backup_migrations());

        module::backup::BackupRepository repo(db);
        module::backup::BackupJob job;
        job.name = "search-hook-test";
        job.source_root = source.string();
        job.destination = destination.string();
        job.schedule = ""; // unscheduled - the snapshot below is seeded directly
        job.retention_keep = 5;
        job.enabled = true;
        job_id = repo.upsert_job(job);

        const auto snapshot_id = repo.begin_snapshot(job_id);
        module::backup::SnapshotFile file;
        file.path = "sub/notes.txt"; // relative, forward-slash - BackupEngine's convention
        file.size = 5;
        file.digest = "deadbeef";
        repo.add_snapshot_files(snapshot_id, std::vector{file});
        repo.finish_snapshot(snapshot_id, "ok", 1, 5, 5);
    }

    RestartableHarness h(db_path, make_scratch_dir("reports"));
    auto owned = std::make_unique<module::backup::BackupModule>();
    module::backup::BackupModule* backup_module = owned.get();
    h.module_host.add(std::move(owned));
    REQUIRE(h.module_host.failures().empty());
    h.module_host.start_enabled();

    REQUIRE(backup_module->is_path_backed_up((source / "sub" / "notes.txt").string()));
    REQUIRE_FALSE(backup_module->is_path_backed_up((source / "sub" / "missing.txt").string()));
    REQUIRE_FALSE(backup_module->is_path_backed_up((base / "elsewhere" / "notes.txt").string()));

    std::error_code ec;
    fs::remove_all(base, ec);
}
