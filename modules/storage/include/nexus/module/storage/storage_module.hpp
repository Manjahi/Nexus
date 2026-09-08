#pragma once

#include <string_view>

#include "nexus/services/module.hpp"
#include "nexus/services/report_center.hpp"

namespace nexus::module::storage {

/// Storage Intelligence module. Scans are user-initiated (run from the UI on the
/// thread pool), so the module only owns the schema and the cleanup report.
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
    nexus::services::ReportCenter::GeneratorId report_id_{};
    bool report_registered_ = false;
};

} // namespace nexus::module::storage
