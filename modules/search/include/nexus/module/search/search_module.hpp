#pragma once

#include "nexus/services/module.hpp"
#include "nexus/services/report_center.hpp"

#include <string_view>

namespace nexus::module::search {

/// Local Search module. Indexing and queries run from the UI on the thread
/// pool; the module owns the schema and the search report.
class SearchModule : public nexus::services::Module {
public:
    SearchModule();
    ~SearchModule() override;

    [[nodiscard]] std::string_view id() const override { return "search"; }
    void apply_migrations(nexus::db::Database& db) override;
    void start(nexus::services::ServiceContext& ctx) override;
    void stop() override;

private:
    nexus::services::ServiceContext* ctx_ = nullptr;
    nexus::services::ReportCenter::GeneratorId report_id_{};
    bool report_registered_ = false;
};

} // namespace nexus::module::search
