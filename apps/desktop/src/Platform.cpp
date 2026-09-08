// std::getenv is safe here (single-threaded startup, result copied immediately).
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "Platform.hpp"

#include <QDir>
#include <QStandardPaths>
#include <QString>

#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "nexus/db/migration.hpp"
#include "nexus/module/connectivity/connectivity_module.hpp"
#include "nexus/module/hardware/hardware_module.hpp"

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

nexus::db::Database open_database(const std::filesystem::path& path) {
    auto db = nexus::db::Database::open(path);
    nexus::db::migrate(db, "core", nexus::db::core_migrations());
    return db;
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
      db_(open_database(db_path_)),
      settings_(db_),
      pool_(0),
      scheduler_(pool_),
      notifications_(500),
      audit_(db_),
      modules_(settings_, default_modules()),
      jobs_(db_),
      notifications_repo_(db_),
      context_{db_,     settings_, pool_,    scheduler_,     notifications_,
               events_, audit_,    modules_, jobs_,          notifications_repo_},
      module_host_(context_) {
    nexus::services::attach_persistence(notifications_, notifications_repo_);

    module_host_.add(std::make_unique<nexus::module::hardware::HardwareModule>());
    module_host_.add(std::make_unique<nexus::module::connectivity::ConnectivityModule>());
    module_host_.start_enabled();

    audit_.record("app_start", to_utf8(db_path_));
}

Platform::~Platform() {
    // Stop modules first (they use the scheduler and db), then detach the sink
    // before its repository dies and stop the timer thread before the pool.
    module_host_.stop_all();
    notifications_.set_persist_sink({});
    scheduler_.stop();
    audit_.record("app_stop");
}

} // namespace nexuspc::desktop
