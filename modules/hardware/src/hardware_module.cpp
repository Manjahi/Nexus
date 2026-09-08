#include "nexus/module/hardware/hardware_module.hpp"

#include <chrono>
#include <memory>

#include "nexus/db/migration.hpp"
#include "nexus/jobs/scheduler.hpp"
#include "nexus/module/hardware/hardware_repository.hpp"
#include "nexus/module/hardware/sampler.hpp"
#include "nexus/services/service_context.hpp"
#include "nexus/system/system_provider.hpp"

namespace nexus::module::hardware {

namespace {
constexpr std::chrono::seconds kSampleInterval{3};
}

HardwareModule::HardwareModule() = default;
HardwareModule::~HardwareModule() = default;

void HardwareModule::apply_migrations(nexus::db::Database& db) {
    nexus::db::migrate(db, "hardware", hardware_migrations());
}

void HardwareModule::start(nexus::services::ServiceContext& ctx) {
    ctx_ = &ctx;

    auto provider = nexus::system::make_system_provider();
    if (provider == nullptr) {
        return; // no provider on this platform
    }

    auto repository = std::make_unique<HardwareRepository>(ctx.db);
    sampler_ = std::make_shared<Sampler>(std::move(provider), std::move(repository),
                                         ctx.notifications);

    std::shared_ptr<Sampler> sampler = sampler_;
    schedule_id_ = ctx.scheduler.schedule_every(kSampleInterval, [sampler] { sampler->tick(); });
    scheduled_ = true;
}

void HardwareModule::stop() {
    if (scheduled_ && ctx_ != nullptr) {
        ctx_->scheduler.cancel(schedule_id_);
        scheduled_ = false;
    }
    if (sampler_) {
        sampler_->set_active(false); // in-flight tick becomes a no-op; it holds its own ref
    }
    sampler_.reset();
}

} // namespace nexus::module::hardware
