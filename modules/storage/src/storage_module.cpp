#include "nexus/module/storage/storage_module.hpp"

#include "nexus/db/migration.hpp"
#include "nexus/db/settings_repository.hpp"
#include "nexus/jobs/scheduler.hpp"
#include "nexus/module/storage/storage_report.hpp"
#include "nexus/module/storage/storage_repository.hpp"
#include "nexus/services/report_center.hpp"
#include "nexus/services/service_context.hpp"

#include <chrono>
#include <string>

namespace nexus::module::storage {

namespace {
constexpr std::chrono::hours kPruneInterval{1};

// UFR-010: how many most-recent scans to keep; older ones (and their
// duplicate_groups/scanned_files rows, via ON DELETE CASCADE) are pruned.
std::size_t keep_scans_setting(nexus::services::ServiceContext& ctx) {
    const std::string raw = ctx.settings.get_or("retention.storage.keep_scans", "20");
    int keep = 20;
    try {
        keep = std::stoi(raw);
    } catch (...) {
        keep = 20;
    }
    return static_cast<std::size_t>(keep < 1 ? 1 : keep);
}
} // namespace

StorageModule::StorageModule() = default;
StorageModule::~StorageModule() = default;

void StorageModule::apply_migrations(nexus::db::Database& db) {
    nexus::db::migrate(db, "storage", storage_migrations());
}

void StorageModule::start(nexus::services::ServiceContext& ctx) {
    ctx_ = &ctx;
    auto* db = &ctx.db;
    report_id_ =
        ctx.reports.register_generator(kStorageCleanupKind, "Storage cleanup", std::string(id()),
                                       [db](nexus::services::ReportFormat format) {
                                           StorageRepository repo(*db);
                                           return render_storage_cleanup(repo, format);
                                       });
    report_registered_ = true;

    const std::size_t keep = keep_scans_setting(ctx);
    schedule_id_ = ctx.scheduler.schedule_every(kPruneInterval, [db, keep] {
        StorageRepository repo(*db);
        repo.prune_scans_keeping(keep);
    });
    scheduled_ = true;
}

bool StorageModule::is_duplicate_file(const std::string& absolute_path) const {
    if (ctx_ == nullptr) {
        return false;
    }
    StorageRepository repo(ctx_->db);
    const auto scan = repo.latest_scan();
    if (!scan) {
        return false;
    }
    for (const GroupRecord& group : repo.groups_for(scan->id)) {
        for (const std::string& file : repo.files_in_group(group.id)) {
            if (file == absolute_path) {
                return true;
            }
        }
    }
    return false;
}

void StorageModule::stop() {
    if (report_registered_ && ctx_ != nullptr) {
        ctx_->reports.unregister(report_id_);
        report_registered_ = false;
    }
    if (scheduled_ && ctx_ != nullptr) {
        ctx_->scheduler.cancel(schedule_id_);
        scheduled_ = false;
    }
}

} // namespace nexus::module::storage
