#pragma once

#include <memory>
#include <string_view>

#include "nexus/jobs/schedule_table.hpp"
#include "nexus/services/module.hpp"

namespace nexus::module::hardware {

class Sampler;

/// System Health module: samples the machine on a fixed cadence into the Health
/// tables and raises threshold notifications.
class HardwareModule : public nexus::services::Module {
public:
    HardwareModule();
    ~HardwareModule() override;

    [[nodiscard]] std::string_view id() const override { return "hardware"; }
    void apply_migrations(nexus::db::Database& db) override;
    void start(nexus::services::ServiceContext& ctx) override;
    void stop() override;

private:
    nexus::services::ServiceContext* ctx_ = nullptr;
    std::shared_ptr<Sampler> sampler_;
    nexus::jobs::ScheduleTable::Id schedule_id_{};
    bool scheduled_ = false;
};

} // namespace nexus::module::hardware
