#pragma once

#include <string>
#include <string_view>

#include "nexus/services/module.hpp"
#include "nexus/services/report_center.hpp"

namespace nexus::module::backup {

inline constexpr const char* kBackupReportKind = "backup-report";

/// Backup & Recovery module. Backups run on demand from the UI on the thread
/// pool; the module owns the schema and the backup report.
class BackupModule : public nexus::services::Module {
public:
    BackupModule();
    ~BackupModule() override;

    [[nodiscard]] std::string_view id() const override { return "backup"; }
    void apply_migrations(nexus::db::Database& db) override;
    void start(nexus::services::ServiceContext& ctx) override;
    void stop() override;

private:
    nexus::services::ServiceContext* ctx_ = nullptr;
    nexus::services::ReportCenter::GeneratorId report_id_{};
    bool report_registered_ = false;
};

} // namespace nexus::module::backup
