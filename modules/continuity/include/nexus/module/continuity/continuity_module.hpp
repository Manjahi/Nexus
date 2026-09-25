#pragma once

#include <string_view>

#include "nexus/services/module.hpp"

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

private:
    nexus::services::ServiceContext* ctx_ = nullptr;
};

} // namespace nexus::module::continuity
