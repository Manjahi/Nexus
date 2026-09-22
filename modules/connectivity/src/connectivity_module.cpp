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
#include "nexus/module/connectivity/speed_test.hpp"
#include "nexus/services/report_center.hpp"
#include "nexus/services/retention_setting.hpp"
#include "nexus/services/service_context.hpp"

namespace nexus::module::connectivity {

namespace {
constexpr std::chrono::seconds kProbeInterval{15};
constexpr std::chrono::hours kSpeedTestInterval{1};
// A ~5MB download is enough to get past slow-start and read a meaningful
// throughput number without costing much bandwidth once an hour. Overridable
// via app_settings so a dead/slow endpoint doesn't need a code change to fix.
constexpr const char* kDefaultSpeedTestUrl = "https://speed.cloudflare.com/__down?bytes=5000000";
} // namespace

ConnectivityModule::ConnectivityModule() = default;
ConnectivityModule::~ConnectivityModule() = default;

void ConnectivityModule::apply_migrations(nexus::db::Database& db) {
    nexus::db::migrate(db, "connectivity", connectivity_migrations());
}

void ConnectivityModule::start(nexus::services::ServiceContext& ctx) {
    ctx_ = &ctx;

    auto repository = std::make_unique<ConnectivityRepository>(ctx.db);
    prober_ = std::make_shared<Prober>(
        std::move(repository), ctx.notifications, &Prober::default_probe, /*outage_after=*/2,
        nexus::services::retention_days_setting(ctx.settings, "retention.connectivity.days", 30));

    std::shared_ptr<Prober> prober = prober_;
    schedule_id_ = ctx.scheduler.schedule_every(
        kProbeInterval, [prober] { prober->tick(); }, kProbeInterval);
    scheduled_ = true;

    const std::string speed_test_url =
        ctx.settings.get_or("connectivity.speedtest.url", kDefaultSpeedTestUrl);
    speed_tester_ =
        std::make_shared<SpeedTester>(std::make_unique<ConnectivityRepository>(ctx.db), speed_test_url);
    std::shared_ptr<SpeedTester> speed_tester = speed_tester_;
    speed_test_schedule_id_ = ctx.scheduler.schedule_every(
        kSpeedTestInterval, [speed_tester] { speed_tester->tick(); }, kSpeedTestInterval);
    speed_test_scheduled_ = true;

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
    if (speed_test_scheduled_ && ctx_ != nullptr) {
        ctx_->scheduler.cancel(speed_test_schedule_id_);
        speed_test_scheduled_ = false;
    }
    if (prober_) {
        prober_->set_active(false);
    }
    prober_.reset();
    if (speed_tester_) {
        speed_tester_->set_active(false);
    }
    speed_tester_.reset();
}

} // namespace nexus::module::connectivity
