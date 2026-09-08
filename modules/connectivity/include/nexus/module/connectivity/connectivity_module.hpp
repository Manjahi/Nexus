#pragma once

#include <memory>
#include <string_view>

#include "nexus/jobs/schedule_table.hpp"
#include "nexus/services/module.hpp"
#include "nexus/services/report_center.hpp"

namespace nexus::module::connectivity {

class Prober;

/// Connectivity Center module: probes configured targets on a fixed cadence,
/// records samples and outages, and notifies on outage transitions.
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
    nexus::services::ReportCenter::GeneratorId report_id_{};
    bool scheduled_ = false;
    bool report_registered_ = false;
};

} // namespace nexus::module::connectivity
