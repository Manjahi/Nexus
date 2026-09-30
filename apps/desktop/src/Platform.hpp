#pragma once

#include "nexus/db/database.hpp"
#include "nexus/db/settings_repository.hpp"
#include "nexus/jobs/schedule_table.hpp"
#include "nexus/jobs/scheduler.hpp"
#include "nexus/jobs/thread_pool.hpp"
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

#include <filesystem>
#include <string>

namespace nexus::module::backup {
class BackupModule;
}
namespace nexus::module::storage {
class StorageModule;
}

namespace nexuspc::desktop {

/// Owns the database and every shared service for the lifetime of the process,
/// and exposes them as one ServiceContext. Construction opens the database,
/// applies migrations, restores persisted notifications, and records app_start.
class Platform {
public:
    Platform();
    ~Platform();

    Platform(const Platform&) = delete;
    Platform& operator=(const Platform&) = delete;

    [[nodiscard]] nexus::services::ServiceContext& context() noexcept { return context_; }
    [[nodiscard]] const std::filesystem::path& database_path() const noexcept { return db_path_; }

    /// The live BackupModule instance, so the UI can call reschedule_job()
    /// when a job is created/edited/enabled without needing a restart.
    /// Never null after construction (added unconditionally, same as every
    /// other in-process module).
    [[nodiscard]] nexus::module::backup::BackupModule* backup_module() const noexcept {
        return backup_module_;
    }

    /// The live StorageModule instance, so the UI can ask whether a Search
    /// result is part of the latest duplicate scan. Never null after
    /// construction, same as backup_module().
    [[nodiscard]] nexus::module::storage::StorageModule* storage_module() const noexcept {
        return storage_module_;
    }

private:
    std::filesystem::path db_path_;
    std::filesystem::path reports_dir_;
    nexus::db::Database db_;
    nexus::db::SettingsRepository settings_;
    nexus::jobs::ThreadPool pool_;
    nexus::jobs::Scheduler scheduler_;
    nexus::notify::NotificationCenter notifications_;
    nexus::services::EventBus events_;
    nexus::services::AuditLog audit_;
    nexus::services::ModuleRegistry modules_;
    nexus::services::JobRepository jobs_;
    nexus::services::NotificationRepository notifications_repo_;
    nexus::services::ReportCenter reports_;
    nexus::services::HeavyJobGuard heavy_jobs_;
    nexus::services::ServiceContext context_;
    nexus::services::ModuleHost module_host_;
    nexus::jobs::ScheduleTable::Id housekeeping_schedule_id_{};
    nexus::module::backup::BackupModule* backup_module_ = nullptr;
    nexus::module::storage::StorageModule* storage_module_ = nullptr;
};

} // namespace nexuspc::desktop
