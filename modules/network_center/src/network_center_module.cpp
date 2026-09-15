#include "nexus/module/network_center/network_center_module.hpp"

#include <chrono>
#include <memory>
#include <string>

#include "nexus/db/migration.hpp"
#include "nexus/db/settings_repository.hpp"
#include "nexus/jobs/scheduler.hpp"
#include "nexus/module/network_center/device_monitor.hpp"
#include "nexus/module/network_center/network_report.hpp"
#include "nexus/module/network_center/network_repository.hpp"
#include "nexus/services/report_center.hpp"
#include "nexus/services/service_context.hpp"

namespace nexus::module::network_center {

namespace {
constexpr std::chrono::seconds kMonitorInterval{60};

// UFR-010: per-module retention, configurable via app_settings.
std::chrono::hours retention_setting(nexus::services::ServiceContext& ctx) {
    const std::string raw = ctx.settings.get_or("retention.network_center.days", "30");
    int days = 30;
    try {
        days = std::stoi(raw);
    } catch (...) {
        days = 30;
    }
    return std::chrono::hours{24 * (days < 1 ? 1 : days)};
}
} // namespace

NetworkCenterModule::NetworkCenterModule() = default;
NetworkCenterModule::~NetworkCenterModule() = default;

void NetworkCenterModule::apply_migrations(nexus::db::Database& db) {
    nexus::db::migrate(db, "network_center", network_center_migrations());
}

void NetworkCenterModule::start(nexus::services::ServiceContext& ctx) {
    ctx_ = &ctx;

    auto repository = std::make_unique<NetworkRepository>(ctx.db);
    monitor_ = std::make_shared<DeviceMonitor>(std::move(repository), ctx.notifications,
                                               &DeviceMonitor::default_ping, retention_setting(ctx));

    std::shared_ptr<DeviceMonitor> monitor = monitor_;
    schedule_id_ = ctx.scheduler.schedule_every(
        kMonitorInterval, [monitor] { monitor->tick(); }, kMonitorInterval);
    scheduled_ = true;

    auto* db = &ctx.db;
    report_id_ = ctx.reports.register_generator(
        kNetworkKind, "Network", std::string(id()), [db](nexus::services::ReportFormat format) {
            NetworkRepository repo(*db);
            return render_network_report(repo, format);
        });
    report_registered_ = true;
}

void NetworkCenterModule::stop() {
    if (report_registered_ && ctx_ != nullptr) {
        ctx_->reports.unregister(report_id_);
        report_registered_ = false;
    }
    if (scheduled_ && ctx_ != nullptr) {
        ctx_->scheduler.cancel(schedule_id_);
        scheduled_ = false;
    }
    if (monitor_) {
        monitor_->set_active(false);
    }
    monitor_.reset();
}

} // namespace nexus::module::network_center
