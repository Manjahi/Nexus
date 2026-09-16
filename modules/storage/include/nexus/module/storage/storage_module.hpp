#pragma once

#include <string_view>

#include "nexus/jobs/schedule_table.hpp"
#include "nexus/services/module.hpp"
#include "nexus/services/report_center.hpp"

namespace nexus::module::storage {

/// Storage Intelligence module. Scans are user-initiated (run from the UI on the
/// thread pool), so the module only owns the schema, the cleanup report, and a
/// periodic prune of old scan history (UFR-010: retention.storage.keep_scans).
class StorageModule : public nexus::services::Module {
public:
    StorageModule();
    ~StorageModule() override;

    [[nodiscard]] std::string_view id() const override { return "storage"; }
    void apply_migrations(nexus::db::Database& db) override;
    void start(nexus::services::ServiceContext& ctx) override;
    void stop() override;

private:
    nexus::services::ServiceContext* ctx_ = nullptr;
    nexus::jobs::ScheduleTable::Id schedule_id_{};
    nexus::services::ReportCenter::GeneratorId report_id_{};
    bool scheduled_ = false;
    bool report_registered_ = false;
};

} // namespace nexus::module::storage
