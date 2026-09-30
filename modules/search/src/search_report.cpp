#include "nexus/module/search/search_report.hpp"

#include "nexus/module/search/search_repository.hpp"
#include "nexus/services/report_format.hpp"

#include <string>

namespace nexus::module::search {

namespace {

using nexus::services::ReportFormat;
using nexus::services::report::csv_cell;
using nexus::services::report::html_document;
using nexus::services::report::html_escape;

std::string render_csv(SearchRepository& repo) {
    std::string out = "path,size,term_count\n";
    for (const IndexedFile& file : repo.all_files()) {
        out += csv_cell(file.path) + "," + std::to_string(file.size) + "," +
               std::to_string(file.term_count) + "\n";
    }
    return out;
}

std::string render_html(SearchRepository& repo) {
    const auto files = repo.all_files();
    std::string body = "<p class=\"muted\">" + std::to_string(files.size()) +
                       " file(s) indexed.</p>"
                       "<table><tr><th>Path</th><th>Size</th><th>Terms</th></tr>";
    for (const IndexedFile& file : files) {
        body += "<tr><td>" + html_escape(file.path) + "</td><td>" + std::to_string(file.size) +
                "</td><td>" + std::to_string(file.term_count) + "</td></tr>";
    }
    body += "</table>";
    return html_document("Search index", body);
}

} // namespace

std::string render_search_report(SearchRepository& repo, ReportFormat format) {
    return format == ReportFormat::Csv ? render_csv(repo) : render_html(repo);
}

} // namespace nexus::module::search
