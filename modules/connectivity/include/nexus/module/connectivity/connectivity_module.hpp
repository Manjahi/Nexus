#pragma once

#include "nexus/jobs/schedule_table.hpp"
#include "nexus/services/module.hpp"
#include "nexus/services/report_center.hpp"

#include <memory>
#include <string_view>

namespace nexus::module::connectivity {

class Prober;
class SpeedTester;

/// Connectivity Center module: probes configured targets on a fixed cadence,
/// records samples and outages, and notifies on outage transitions. Also
/// runs a much less frequent timed-download speed test (see speed_test.hpp).
class ConnectivityModule : public nexus::services::Module {
public:
    ConnectivityModule();
    ~ConnectivityModule() override;

    [[nodiscard]] std::string_view id() const override { return "connectivity"; }
    void apply_migrations(nexus::db::Database& db) override;
    void start(nexus::services::ServiceContext& ctx) override;
    void stop() override;

private:
    nexus::services::ServiceContext* ctx_ = nullptr;
    std::shared_ptr<Prober> prober_;
    nexus::jobs::ScheduleTable::Id schedule_id_{};
    std::shared_ptr<SpeedTester> speed_tester_;
    nexus::jobs::ScheduleTable::Id speed_test_schedule_id_{};
    nexus::services::ReportCenter::GeneratorId report_id_{};
    bool scheduled_ = false;
    bool speed_test_scheduled_ = false;
    bool report_registered_ = false;
};

} // namespace nexus::module::connectivity
