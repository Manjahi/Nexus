#pragma once

#include <filesystem>
#include <string>

#include "nexus/db/database.hpp"
#include "nexus/db/settings_repository.hpp"
#include "nexus/jobs/scheduler.hpp"
#include "nexus/jobs/thread_pool.hpp"
#include "nexus/notify/notification_center.hpp"
#include "nexus/services/audit_log.hpp"
#include "nexus/services/event_bus.hpp"
#include "nexus/services/job_repository.hpp"
#include "nexus/services/module_registry.hpp"
#include "nexus/services/notification_repository.hpp"
#include "nexus/services/service_context.hpp"

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

private:
    std::filesystem::path db_path_;
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
    nexus::services::ServiceContext context_;
};

} // namespace nexuspc::desktop
