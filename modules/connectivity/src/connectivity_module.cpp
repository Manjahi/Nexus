#include "nexus/module/connectivity/connectivity_module.hpp"

#include <chrono>
#include <memory>
#include <string>

#include "nexus/db/migration.hpp"
#include "nexus/db/settings_repository.hpp"
#include "nexus/jobs/scheduler.hpp"
#include "nexus/module/connectivity/connectivity_report.hpp"
#include "nexus/module/connectivity/connectivity_repository.hpp"
#include "nexus/module/connectivity/prober.hpp"
#include "nexus/services/report_center.hpp"
#include "nexus/services/service_context.hpp"

namespace nexus::module::connectivity {

namespace {
constexpr std::chrono::seconds kProbeInterval{15};

// UFR-010: per-module retention, configurable via app_settings.
std::chrono::hours retention_setting(nexus::services::ServiceContext& ctx) {
    const std::string raw = ctx.settings.get_or("retention.connectivity.days", "30");
    int days = 30;
    try {
        days = std::stoi(raw);
    } catch (...) {
        days = 30;
    }
    return std::chrono::hours{24 * (days < 1 ? 1 : days)};
}
} // namespace

ConnectivityModule::ConnectivityModule() = default;
ConnectivityModule::~ConnectivityModule() = default;

void ConnectivityModule::apply_migrations(nexus::db::Database& db) {
    nexus::db::migrate(db, "connectivity", connectivity_migrations());
}

void ConnectivityModule::start(nexus::services::ServiceContext& ctx) {
    ctx_ = &ctx;

    auto repository = std::make_unique<ConnectivityRepository>(ctx.db);
    prober_ = std::make_shared<Prober>(std::move(repository), ctx.notifications,
                                       &Prober::default_probe, /*outage_after=*/2,
                                       retention_setting(ctx));

    std::shared_ptr<Prober> prober = prober_;
    schedule_id_ = ctx.scheduler.schedule_every(
        kProbeInterval, [prober] { prober->tick(); }, kProbeInterval);
    scheduled_ = true;

    auto* db = &ctx.db;
    report_id_ = ctx.reports.register_generator(
        kInternetReliabilityKind, "Internet reliability", std::string(id()),
        [db](nexus::services::ReportFormat format) {
            ConnectivityRepository repo(*db);
            return render_internet_reliability(repo, format);
        });
    report_registered_ = true;
}

void ConnectivityModule::stop() {
    if (report_registered_ && ctx_ != nullptr) {
        ctx_->reports.unregister(report_id_);
        report_registered_ = false;
    }
    if (scheduled_ && ctx_ != nullptr) {
        ctx_->scheduler.cancel(schedule_id_);
        scheduled_ = false;
    }
    if (prober_) {
        prober_->set_active(false);
    }
    prober_.reset();
}

} // namespace nexus::module::connectivity
