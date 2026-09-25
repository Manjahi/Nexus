#include "nexus/module/continuity/continuity_module.hpp"

#include "nexus/db/migration.hpp"
#include "nexus/module/continuity/continuity_repository.hpp"
#include "nexus/services/service_context.hpp"

namespace nexus::module::continuity {

ContinuityModule::ContinuityModule() = default;
ContinuityModule::~ContinuityModule() = default;

void ContinuityModule::apply_migrations(nexus::db::Database& db) {
    nexus::db::migrate(db, "continuity", continuity_migrations());
}

void ContinuityModule::start(nexus::services::ServiceContext& ctx) {
    ctx_ = &ctx;
}

void ContinuityModule::stop() {
    ctx_ = nullptr;
}

} // namespace nexus::module::continuity
