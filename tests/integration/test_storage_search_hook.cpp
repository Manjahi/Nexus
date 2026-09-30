// Spec section 9 hook #6 (Search->Storage "is this a duplicate?"):
// StorageModule::is_duplicate_file() needs a real ServiceContext (it reads
// ctx_->db, set only by start()), so this exercises it the same way
// test_backup_schedule_restart.cpp exercises BackupModule - a real
// ModuleHost/ServiceContext over a temp on-disk database.

#include "nexus/db/database.hpp"
#include "nexus/db/migration.hpp"
#include "nexus/db/settings_repository.hpp"
#include "nexus/jobs/scheduler.hpp"
#include "nexus/jobs/thread_pool.hpp"
#include "nexus/module/storage/storage_module.hpp"
#include "nexus/module/storage/storage_repository.hpp"
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
#include <memory>
#include <string>

using namespace nexus;
// Named stdfs, not fs - nexus::fs (pulled in by
// nexus/module/storage/duplicate_scanner.hpp, included transitively via
// storage_module.hpp) would otherwise collide with a `fs` alias here.
namespace stdfs = std::filesystem;

namespace {

struct StorageHarness {
    stdfs::path reports_dir;
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

    explicit StorageHarness(const stdfs::path& db_path, stdfs::path reports_dir_)
        : reports_dir(std::move(reports_dir_)), db(db::Database::open(db_path)),
          modules(settings, {{"storage", "Storage Intelligence", true}}), reports(db, reports_dir),
          ctx{db,    settings, pool,      scheduler,          notifications, events,
              audit, modules,  jobs_repo, notifications_repo, reports,       heavy_jobs},
          module_host(ctx) {
        db::migrate(db, "core", db::core_migrations());
        db::migrate(db, "storage", module::storage::storage_migrations());
        std::filesystem::create_directories(reports_dir);
        services::attach_persistence(notifications, notifications_repo);
    }

    ~StorageHarness() {
        module_host.stop_all();
        scheduler.stop();
        pool.wait_idle();
        std::error_code ec;
        std::filesystem::remove_all(reports_dir, ec);
    }
};

stdfs::path make_scratch_dir(std::string_view tag) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return stdfs::temp_directory_path() /
           ("nexuspc_storage_search_" + std::string(tag) + "_" + std::to_string(stamp));
}

} // namespace

TEST_CASE("is_duplicate_file matches an absolute path against the latest scan's groups",
          "[integration][storage][search]") {
    const stdfs::path base = make_scratch_dir("base");
    stdfs::create_directories(base);
    const stdfs::path db_path = base / "nexus.db";
    const std::string one = (base / "one.txt").generic_string();
    const std::string two = (base / "sub" / "two.txt").generic_string();
    const std::string unrelated = (base / "unrelated.txt").generic_string();

    {
        auto db = db::Database::open(db_path);
        db::migrate(db, "core", db::core_migrations());
        db::migrate(db, "storage", module::storage::storage_migrations());

        module::storage::StorageRepository repo(db);
        const auto scan_id = repo.begin_scan(base.string());
        module::storage::DuplicateGroup group;
        group.digest = "deadbeef";
        group.files = {stdfs::path(one), stdfs::path(two)};
        repo.add_group(scan_id, group);
        module::storage::ScanSummary summary;
        summary.files_seen = 3;
        summary.groups = {group};
        repo.finish_scan(scan_id, summary);
    }

    StorageHarness h(db_path, make_scratch_dir("reports"));
    auto owned = std::make_unique<module::storage::StorageModule>();
    module::storage::StorageModule* storage_module = owned.get();
    h.module_host.add(std::move(owned));
    REQUIRE(h.module_host.failures().empty());
    h.module_host.start_enabled();

    REQUIRE(storage_module->is_duplicate_file(one));
    REQUIRE(storage_module->is_duplicate_file(two));
    REQUIRE_FALSE(storage_module->is_duplicate_file(unrelated));

    std::error_code ec;
    stdfs::remove_all(base, ec);
}
