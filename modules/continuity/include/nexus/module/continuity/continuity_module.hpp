#pragma once

#include "nexus/module/continuity/continuity_readiness.hpp"
#include "nexus/module/continuity/continuity_scenarios.hpp"
#include "nexus/services/module.hpp"
#include "nexus/services/report_center.hpp"

#include <string_view>

namespace nexus::module::continuity {

/// Continuity module: owns the tracked-assets/rehearsals schema. Readiness
/// scoring and scenario evaluation (which also read Backup/Vault/Storage
/// state) are added as plain query methods here as they land - never new
/// EventBus events, matching how BackupModule::is_path_backed_up() reaches
/// into another module's repository directly. Real rehearsals and Recovery
/// Capsule exports are triggered from the desktop layer (which owns the
/// thread pool and Vault access), not run inside this module.
class ContinuityModule : public nexus::services::Module {
public:
    ContinuityModule();
    ~ContinuityModule() override;

    [[nodiscard]] std::string_view id() const override { return "continuity"; }
    void apply_migrations(nexus::db::Database& db) override;
    void start(nexus::services::ServiceContext& ctx) override;
    void stop() override;

    /// Empty (tracked_count == 0) if called before start(). See
    /// continuity_readiness.hpp for the scoring formula.
    [[nodiscard]] ReadinessReport compute_readiness() const;
    /// Empty if called before start(). See continuity_scenarios.hpp.
    [[nodiscard]] std::vector<ScenarioStatus> evaluate_scenarios() const;

private:
    nexus::services::ServiceContext* ctx_ = nullptr;
    nexus::services::ReportCenter::GeneratorId report_id_{};
    bool report_registered_ = false;
};

} // namespace nexus::module::continuity
