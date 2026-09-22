// std::getenv is safe here (single-threaded startup, result copied immediately).
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "Platform.hpp"

#include <QDir>
#include <QStandardPaths>
#include <QString>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "nexus/core/time.hpp"
#include "nexus/db/migration.hpp"
#include "nexus/module/backup/backup_module.hpp"
#include "nexus/module/connectivity/connectivity_module.hpp"
#include "nexus/module/hardware/hardware_module.hpp"
#include "nexus/module/network_center/network_center_module.hpp"
#include "nexus/module/search/search_module.hpp"
#include "nexus/module/storage/storage_module.hpp"

namespace nexuspc::desktop {

namespace {

std::string to_utf8(const std::filesystem::path& path) {
    const std::u8string bytes = path.u8string();
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

std::filesystem::path resolve_database_path() {
    if (const char* override_path = std::getenv("NEXUSPC_DB")) {
        if (override_path[0] != '\0') {
            return std::filesystem::path(override_path);
        }
    }
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty()) {
        dir = QDir::homePath() + "/.nexuspc";
    }
    QDir().mkpath(dir);
    const QString file = QDir(dir).filePath(QStringLiteral("nexuspc.db"));
    return std::filesystem::path(file.toStdWString());
}

std::filesystem::path resolve_reports_dir(const std::filesystem::path& db_path) {
    return db_path.parent_path() / "reports";
}

nexus::db::Database open_database(const std::filesystem::path& path) {
    auto db = nexus::db::Database::open(path);
    nexus::db::migrate(db, "core", nexus::db::core_migrations());
    return db;
}

constexpr std::chrono::hours kHousekeepingInterval{1};

// UFR-010: unlike per-module retention, job_runs/notifications/reports
// aren't owned by any one module - see docs/UFR_CONFORMANCE.md's
// "process-lifecycle gaps" for why this was missing entirely before.
std::chrono::hours core_retention_setting(nexus::db::SettingsRepository& settings) {
    const std::string raw = settings.get_or("retention.core.days", "30");
    int days = 30;
    try {
        days = std::stoi(raw);
    } catch (...) {
        days = 30;
    }
    return std::chrono::hours{24 * (days < 1 ? 1 : days)};
}

std::vector<nexus::services::ModuleInfo> default_modules() {
    return {
        {"storage", "Storage Intelligence", true},
        {"connectivity", "Connectivity Center", true},
        {"hardware", "System Health", true},
        {"network_center", "Network Center", false},
        {"backup", "Backup & Recovery", false},
        {"search", "Local Search", false},
        {"vault", "Secure Vault", false},
    };
}

} // namespace

Platform::Platform()
    : db_path_(resolve_database_path()),
      reports_dir_(resolve_reports_dir(db_path_)),
      db_(open_database(db_path_)),
      settings_(db_),
      pool_(0),
      scheduler_(pool_),
      notifications_(500),
      audit_(db_),
      modules_(settings_, default_modules()),
      jobs_(db_),
      notifications_repo_(db_),
      reports_(db_, reports_dir_),
      heavy_jobs_(),
      context_{db_,     settings_, pool_,       scheduler_,          notifications_,
               events_, audit_,    modules_,    jobs_,               notifications_repo_,
               reports_, heavy_jobs_},
      module_host_(context_) {
    nexus::services::attach_persistence(notifications_, notifications_repo_);

    auto storage_module = std::make_unique<nexus::module::storage::StorageModule>();
    storage_module_ = storage_module.get();
    module_host_.add(std::move(storage_module));
    auto backup_module = std::make_unique<nexus::module::backup::BackupModule>();
    backup_module_ = backup_module.get();
    module_host_.add(std::move(backup_module));
    module_host_.add(std::make_unique<nexus::module::search::SearchModule>());
    module_host_.add(std::make_unique<nexus::module::hardware::HardwareModule>());
    module_host_.add(std::make_unique<nexus::module::connectivity::ConnectivityModule>());
    module_host_.add(std::make_unique<nexus::module::network_center::NetworkCenterModule>());
    module_host_.start_enabled();

    const auto retention = core_retention_setting(settings_);
    auto* jobs = &jobs_;
    auto* notifications_repo = &notifications_repo_;
    auto* reports = &reports_;
    housekeeping_schedule_id_ =
        scheduler_.schedule_every(kHousekeepingInterval, [jobs, notifications_repo, reports, retention] {
            const auto cutoff = nexus::core::now() - retention;
            jobs->prune_finished_runs_before(cutoff);
            notifications_repo->prune_before(cutoff);
            reports->prune_before(cutoff);
        });

    audit_.record("app_start", to_utf8(db_path_));
}

Platform::~Platform() {
    // Stop modules first (they use the scheduler and db), then detach the sink
    // before its repository dies and stop the timer thread before the pool.
    module_host_.stop_all();
    notifications_.set_persist_sink({});
    scheduler_.stop();

    // A tick already handed to the pool when stop_all()/scheduler_.stop() ran
    // (in flight, or merely queued) isn't cancelled by either of those - it
    // still runs to completion, touching notifications_/db_/audit_ etc.
    // Member destructors below run in reverse declaration order, and pool_
    // is declared *before* several of those services; ThreadPool's own
    // destructor would otherwise drain (run) that leftover task after they're
    // already gone. wait_idle() here forces it to finish now, while
    // everything it touches is still alive. Caught by an integration test
    // that - unlike anything before it - actually waited long enough for a
    // real recurring tick to be in flight at shutdown.
    pool_.wait_idle();

    audit_.record("app_stop");
}

} // namespace nexuspc::desktop
