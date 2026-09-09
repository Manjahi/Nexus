#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "nexus/jobs/schedule_table.hpp"
#include "nexus/services/module.hpp"
#include "nexus/services/report_center.hpp"

namespace nexus::module::backup {

class ScheduledBackup;

inline constexpr const char* kBackupReportKind = "backup-report";

/// Parses a job schedule string ("every 6h", "every 30m", "every 1d"). Returns
/// nullopt for an empty or unrecognised value.
[[nodiscard]] std::optional<std::chrono::seconds> parse_schedule(std::string_view text);

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

    std::vector<std::shared_ptr<ScheduledBackup>> scheduled_;
    std::vector<nexus::jobs::ScheduleTable::Id> schedule_ids_;
};

} // namespace nexus::module::backup
