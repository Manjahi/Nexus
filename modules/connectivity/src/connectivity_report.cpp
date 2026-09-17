#include "nexus/module/connectivity/connectivity_report.hpp"

#include <chrono>
#include <optional>
#include <string>

#include "nexus/core/time.hpp"
#include "nexus/module/connectivity/connectivity_repository.hpp"
#include "nexus/services/report_format.hpp"

namespace nexus::module::connectivity {

namespace {

using nexus::services::ReportFormat;
using nexus::services::report::csv_cell;
using nexus::services::report::html_document;
using nexus::services::report::html_escape;
using nexus::services::report::number;

std::string uptime_text(const std::optional<double>& fraction) {
    return fraction ? number(*fraction * 100.0, 1) + "%" : std::string("n/a");
}

std::string render_csv(ConnectivityRepository& repo, nexus::core::Timestamp since) {
    std::string out = "section,id,label,value\n";
    for (const ProbeTarget& t : repo.targets()) {
        out += "uptime," + csv_cell(t.id) + "," + csv_cell(t.label) + "," +
               uptime_text(repo.uptime_fraction(t.id, since)) + "\n";
    }
    for (const Outage& o : repo.recent_outages(50)) {
        out += "outage," + csv_cell(o.target_id) + "," +
               csv_cell(nexus::core::to_iso8601(o.started_at)) + "," +
               (o.ended_at ? nexus::core::to_iso8601(*o.ended_at) : std::string("ongoing")) + "\n";
    }
    return out;
}

std::string render_html(ConnectivityRepository& repo, nexus::core::Timestamp since) {
    std::string body =
        "<p class=\"muted\">Window: last 24 hours.</p>"
        "<h2>Targets</h2><table>"
        "<tr><th>Target</th><th>Address</th><th>Uptime</th></tr>";
    for (const ProbeTarget& t : repo.targets()) {
        const std::string label = t.label.empty() ? t.id : t.label;
        body += "<tr><td>" + html_escape(label) + "</td><td>" + html_escape(t.address) + "</td><td>" +
               uptime_text(repo.uptime_fraction(t.id, since)) + "</td></tr>";
    }
    body +=
        "</table><h2>Recent outages</h2><table>"
        "<tr><th>Target</th><th>Started</th><th>Ended</th><th>Failed samples</th></tr>";
    for (const Outage& o : repo.recent_outages(50)) {
        const bool ongoing = !o.ended_at.has_value();
        body += "<tr><td>" + html_escape(o.target_id) + "</td><td>" +
               html_escape(nexus::core::to_iso8601(o.started_at)) + "</td><td>" +
               (ongoing ? "<span class=\"status-warning\">ongoing</span>"
                        : html_escape(nexus::core::to_iso8601(*o.ended_at))) +
               "</td><td>" + std::to_string(o.samples_failed) + "</td></tr>";
    }
    body += "</table>";
    return html_document("Internet reliability", body);
}

} // namespace

std::string render_internet_reliability(ConnectivityRepository& repo, ReportFormat format) {
    const auto since = nexus::core::now() - std::chrono::hours{24};
    return format == ReportFormat::Csv ? render_csv(repo, since) : render_html(repo, since);
}

} // namespace nexus::module::connectivity
