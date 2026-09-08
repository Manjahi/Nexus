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
    std::string out =
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<title>Internet reliability</title></head><body>"
        "<h1>Internet reliability</h1><p>Window: last 24 hours.</p>"
        "<h2>Targets</h2><table border=\"1\" cellpadding=\"4\">"
        "<tr><th>Target</th><th>Address</th><th>Uptime</th></tr>";
    for (const ProbeTarget& t : repo.targets()) {
        const std::string label = t.label.empty() ? t.id : t.label;
        out += "<tr><td>" + html_escape(label) + "</td><td>" + html_escape(t.address) + "</td><td>" +
               uptime_text(repo.uptime_fraction(t.id, since)) + "</td></tr>";
    }
    out +=
        "</table><h2>Recent outages</h2><table border=\"1\" cellpadding=\"4\">"
        "<tr><th>Target</th><th>Started</th><th>Ended</th><th>Failed samples</th></tr>";
    for (const Outage& o : repo.recent_outages(50)) {
        out += "<tr><td>" + html_escape(o.target_id) + "</td><td>" +
               html_escape(nexus::core::to_iso8601(o.started_at)) + "</td><td>" +
               (o.ended_at ? html_escape(nexus::core::to_iso8601(*o.ended_at))
                           : std::string("ongoing")) +
               "</td><td>" + std::to_string(o.samples_failed) + "</td></tr>";
    }
    out += "</table></body></html>";
    return out;
}

} // namespace

std::string render_internet_reliability(ConnectivityRepository& repo, ReportFormat format) {
    const auto since = nexus::core::now() - std::chrono::hours{24};
    return format == ReportFormat::Csv ? render_csv(repo, since) : render_html(repo, since);
}

} // namespace nexus::module::connectivity
