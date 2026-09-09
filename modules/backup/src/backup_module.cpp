#include "nexus/module/backup/backup_module.hpp"

#include <string>

#include "nexus/core/time.hpp"
#include "nexus/db/migration.hpp"
#include "nexus/module/backup/backup_repository.hpp"
#include "nexus/services/report_center.hpp"
#include "nexus/services/report_format.hpp"
#include "nexus/services/service_context.hpp"

namespace nexus::module::backup {

namespace {

using nexus::services::ReportFormat;

std::string mib(std::uint64_t bytes) {
    return nexus::services::report::number(static_cast<double>(bytes) / (1024.0 * 1024.0), 1);
}

std::string render(BackupRepository& repo, ReportFormat format) {
    const auto jobs = repo.list_jobs();
    if (format == ReportFormat::Csv) {
        std::string out = "job,source,last_snapshot,state,files,total_mib,new_mib\n";
        for (const auto& job : jobs) {
            const auto snap = repo.latest_snapshot(job.id);
            out += nexus::services::report::csv_cell(job.name.empty() ? job.source_root : job.name) +
                   "," + nexus::services::report::csv_cell(job.source_root) + "," +
                   (snap ? nexus::core::to_iso8601(snap->started_at) : std::string("never")) + "," +
                   (snap ? snap->state : std::string("-")) + "," +
                   (snap ? std::to_string(snap->file_count) : std::string("0")) + "," +
                   (snap ? mib(snap->total_bytes) : std::string("0")) + "," +
                   (snap ? mib(snap->new_bytes) : std::string("0")) + "\n";
        }
        return out;
    }

    std::string out =
        "<!doctype html><html><head><meta charset=\"utf-8\"><title>Backup report</title></head>"
        "<body><h1>Backup report</h1><table border=\"1\" cellpadding=\"4\">"
        "<tr><th>Job</th><th>Source</th><th>Last snapshot</th><th>State</th><th>Files</th>"
        "<th>Total (MiB)</th><th>New (MiB)</th></tr>";
    for (const auto& job : jobs) {
        const auto snap = repo.latest_snapshot(job.id);
        out += "<tr><td>" +
               nexus::services::report::html_escape(job.name.empty() ? job.source_root : job.name) +
               "</td><td>" + nexus::services::report::html_escape(job.source_root) + "</td><td>" +
               (snap ? nexus::core::to_iso8601(snap->started_at) : std::string("never")) +
               "</td><td>" + (snap ? snap->state : std::string("-")) + "</td><td>" +
               (snap ? std::to_string(snap->file_count) : std::string("0")) + "</td><td>" +
               (snap ? mib(snap->total_bytes) : std::string("0")) + "</td><td>" +
               (snap ? mib(snap->new_bytes) : std::string("0")) + "</td></tr>";
    }
    out += "</table></body></html>";
    return out;
}

} // namespace

BackupModule::BackupModule() = default;
BackupModule::~BackupModule() = default;

void BackupModule::apply_migrations(nexus::db::Database& db) {
    nexus::db::migrate(db, "backup", backup_migrations());
}

void BackupModule::start(nexus::services::ServiceContext& ctx) {
    ctx_ = &ctx;
    auto* db = &ctx.db;
    report_id_ = ctx.reports.register_generator(
        kBackupReportKind, "Backup report", std::string(id()),
        [db](ReportFormat format) {
            BackupRepository repo(*db);
            return render(repo, format);
        });
    report_registered_ = true;
}

void BackupModule::stop() {
    if (report_registered_ && ctx_ != nullptr) {
        ctx_->reports.unregister(report_id_);
        report_registered_ = false;
    }
}

} // namespace nexus::module::backup
