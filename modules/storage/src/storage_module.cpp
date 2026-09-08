#include "nexus/module/storage/storage_module.hpp"

#include <string>

#include "nexus/db/migration.hpp"
#include "nexus/module/storage/storage_report.hpp"
#include "nexus/module/storage/storage_repository.hpp"
#include "nexus/services/report_center.hpp"
#include "nexus/services/service_context.hpp"

namespace nexus::module::storage {

StorageModule::StorageModule() = default;
StorageModule::~StorageModule() = default;

void StorageModule::apply_migrations(nexus::db::Database& db) {
    nexus::db::migrate(db, "storage", storage_migrations());
}

void StorageModule::start(nexus::services::ServiceContext& ctx) {
    ctx_ = &ctx;
    auto* db = &ctx.db;
    report_id_ = ctx.reports.register_generator(
        kStorageCleanupKind, "Storage cleanup", std::string(id()),
        [db](nexus::services::ReportFormat format) {
            StorageRepository repo(*db);
            return render_storage_cleanup(repo, format);
        });
    report_registered_ = true;
}

void StorageModule::stop() {
    if (report_registered_ && ctx_ != nullptr) {
        ctx_->reports.unregister(report_id_);
        report_registered_ = false;
    }
}

} // namespace nexus::module::storage
