#include "nexus/module/storage/storage_report.hpp"

#include <string>

#include "nexus/core/time.hpp"
#include "nexus/module/storage/storage_repository.hpp"
#include "nexus/services/report_format.hpp"

namespace nexus::module::storage {

namespace {

using nexus::services::ReportFormat;
using nexus::services::report::csv_cell;
using nexus::services::report::html_escape;

std::string mib(std::uint64_t bytes) {
    return nexus::services::report::number(static_cast<double>(bytes) / (1024.0 * 1024.0), 1);
}

std::string render_csv(StorageRepository& repo) {
    std::string out = "section,key,value\n";
    if (const auto scan = repo.latest_scan()) {
        out += "scan,root," + csv_cell(scan->root) + "\n";
        out += "scan,files_seen," + std::to_string(scan->files_seen) + "\n";
        out += "scan,duplicate_groups," + std::to_string(scan->duplicate_groups) + "\n";
        out += "scan,reclaimable_mib," + mib(scan->reclaimable_bytes) + "\n";
        for (const GroupRecord& g : repo.groups_for(scan->id)) {
            out += "group," + csv_cell(g.digest) + "," + std::to_string(g.file_count) + " files, " +
                   mib(g.reclaimable_bytes) + " MiB\n";
        }
    }
    return out;
}

std::string render_html(StorageRepository& repo) {
    std::string out =
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<title>Storage cleanup</title></head><body><h1>Storage cleanup</h1>";
    const auto scan = repo.latest_scan();
    if (!scan) {
        out += "<p>No scans yet.</p></body></html>";
        return out;
    }
    out += "<p>Root: " + html_escape(scan->root) + "<br>Files seen: " +
           std::to_string(scan->files_seen) + "<br>Duplicate groups: " +
           std::to_string(scan->duplicate_groups) + "<br>Reclaimable: " +
           mib(scan->reclaimable_bytes) + " MiB</p>";
    out +=
        "<table border=\"1\" cellpadding=\"4\"><tr><th>Digest</th><th>Files</th>"
        "<th>Size (MiB)</th><th>Reclaimable (MiB)</th></tr>";
    for (const GroupRecord& g : repo.groups_for(scan->id)) {
        out += "<tr><td>" + html_escape(g.digest.substr(0, 16)) + "&hellip;</td><td>" +
               std::to_string(g.file_count) + "</td><td>" + mib(g.file_size) + "</td><td>" +
               mib(g.reclaimable_bytes) + "</td></tr>";
    }
    out += "</table></body></html>";
    return out;
}

} // namespace

std::string render_storage_cleanup(StorageRepository& repo, ReportFormat format) {
    return format == ReportFormat::Csv ? render_csv(repo) : render_html(repo);
}

} // namespace nexus::module::storage
