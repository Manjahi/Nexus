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
        : reports_dir(std::move(reports_dir_)),
          db(db::Database::open(db_path)),
          modules(settings, {{"backup", "Backup & Recovery", true}}),
          reports(db, reports_dir),
          ctx{db,     settings, pool,     scheduler, notifications, events,
              audit,  modules,  jobs_repo, notifications_repo, reports, heavy_jobs},
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
