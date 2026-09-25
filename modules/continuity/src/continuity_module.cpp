#include "nexus/module/continuity/continuity_module.hpp"

#include "nexus/db/migration.hpp"
#include "nexus/module/continuity/continuity_report.hpp"
#include "nexus/module/continuity/continuity_repository.hpp"
#include "nexus/services/service_context.hpp"

#include <string>

namespace nexus::module::continuity {

ContinuityModule::ContinuityModule() = default;
ContinuityModule::~ContinuityModule() = default;

void ContinuityModule::apply_migrations(nexus::db::Database& db) {
    nexus::db::migrate(db, "continuity", continuity_migrations());
}

void ContinuityModule::start(nexus::services::ServiceContext& ctx) {
    ctx_ = &ctx;
    auto* db = &ctx.db;
    report_id_ = ctx.reports.register_generator(
        kRecoveryPlanKind, "Recovery plan", std::string(id()),
        [db](nexus::services::ReportFormat format) { return render_recovery_plan(*db, format); });
    report_registered_ = true;
}

void ContinuityModule::stop() {
    if (report_registered_ && ctx_ != nullptr) {
        ctx_->reports.unregister(report_id_);
        report_registered_ = false;
    }
    ctx_ = nullptr;
}

ReadinessReport ContinuityModule::compute_readiness() const {
    if (ctx_ == nullptr) {
        return {};
    }
    return nexus::module::continuity::compute_readiness(ctx_->db);
}

std::vector<ScenarioStatus> ContinuityModule::evaluate_scenarios() const {
    if (ctx_ == nullptr) {
        return {};
    }
    return nexus::module::continuity::evaluate_scenarios(ctx_->db);
}

} // namespace nexus::module::continuity
