#include "nexus/module/hardware/hardware_report.hpp"

#include "nexus/module/hardware/hardware_repository.hpp"
#include "nexus/services/report_format.hpp"

#include <string>

namespace nexus::module::hardware {

namespace {

using nexus::services::ReportFormat;
using nexus::services::report::csv_cell;
using nexus::services::report::html_document;
using nexus::services::report::html_escape;
using nexus::services::report::number;

std::string render_csv(HardwareRepository& repo) {
    std::string out = "section,name,scope,value\n";
    for (const MetricSample& m : repo.latest_snapshot()) {
        out +=
            "metric," + csv_cell(m.metric) + "," + csv_cell(m.scope) + "," + number(m.value) + "\n";
    }
    for (const ProcessSample& p : repo.latest_processes(15)) {
        out += "process," + csv_cell(p.name) + "," + std::to_string(p.pid) + "," +
               number(p.cpu_fraction) + "\n";
    }
    return out;
}

std::string render_html(HardwareRepository& repo) {
    std::string body = "<h2>Current metrics</h2>"
                       "<table><tr><th>Metric</th><th>Scope</th><th>Value</th></tr>";
    for (const MetricSample& m : repo.latest_snapshot()) {
        body += "<tr><td>" + html_escape(m.metric) + "</td><td>" + html_escape(m.scope) +
                "</td><td>" + number(m.value) + "</td></tr>";
    }
    body += "</table><h2>Top processes</h2>"
            "<table><tr><th>Process</th><th>PID</th>"
            "<th>CPU</th><th>Working set (MB)</th></tr>";
    for (const ProcessSample& p : repo.latest_processes(15)) {
        body += "<tr><td>" + html_escape(p.name) + "</td><td>" + std::to_string(p.pid) +
                "</td><td>" + number(p.cpu_fraction) + "</td><td>" +
                number(static_cast<double>(p.working_set_bytes) / (1024.0 * 1024.0), 1) +
                "</td></tr>";
    }
    body += "</table>";
    return html_document("System diagnostic", body);
}

} // namespace

std::string render_system_diagnostic(HardwareRepository& repo, ReportFormat format) {
    return format == ReportFormat::Csv ? render_csv(repo) : render_html(repo);
}

} // namespace nexus::module::hardware
