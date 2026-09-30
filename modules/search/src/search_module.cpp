#include "nexus/module/search/search_module.hpp"

#include "nexus/db/migration.hpp"
#include "nexus/module/search/search_report.hpp"
#include "nexus/module/search/search_repository.hpp"
#include "nexus/services/service_context.hpp"

#include <string>

namespace nexus::module::search {

SearchModule::SearchModule() = default;
SearchModule::~SearchModule() = default;

void SearchModule::apply_migrations(nexus::db::Database& db) {
    nexus::db::migrate(db, "search", search_migrations());
}

void SearchModule::start(nexus::services::ServiceContext& ctx) {
    ctx_ = &ctx;
    auto* db = &ctx.db;
    report_id_ =
        ctx.reports.register_generator(kSearchReportKind, "Search index", std::string(id()),
                                       [db](nexus::services::ReportFormat format) {
                                           SearchRepository repo(*db);
                                           return render_search_report(repo, format);
                                       });
    report_registered_ = true;
}

void SearchModule::stop() {
    if (report_registered_ && ctx_ != nullptr) {
        ctx_->reports.unregister(report_id_);
        report_registered_ = false;
    }
}

} // namespace nexus::module::search
