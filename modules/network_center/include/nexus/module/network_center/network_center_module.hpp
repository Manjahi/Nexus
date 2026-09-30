#pragma once

#include "nexus/jobs/schedule_table.hpp"
#include "nexus/services/module.hpp"
#include "nexus/services/report_center.hpp"

#include <memory>
#include <string_view>

namespace nexus::module::network_center {

class DeviceMonitor;

/// Network Center module: re-pings user-authorized devices on a fixed cadence,
/// records availability history, and notifies on online/offline transitions.
/// Discovery of new devices (NetworkScanner) only ever runs at the user's
/// explicit request, from the desktop UI - never automatically.
class NetworkCenterModule : public nexus::services::Module {
public:
    NetworkCenterModule();
    ~NetworkCenterModule() override;

    [[nodiscard]] std::string_view id() const override { return "network_center"; }
    void apply_migrations(nexus::db::Database& db) override;
    void start(nexus::services::ServiceContext& ctx) override;
    void stop() override;

private:
    nexus::services::ServiceContext* ctx_ = nullptr;
    std::shared_ptr<DeviceMonitor> monitor_;
    nexus::jobs::ScheduleTable::Id schedule_id_{};
    nexus::services::ReportCenter::GeneratorId report_id_{};
    bool scheduled_ = false;
    bool report_registered_ = false;
};

} // namespace nexus::module::network_center
