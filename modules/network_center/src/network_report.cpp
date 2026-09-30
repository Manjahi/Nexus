#include "nexus/module/network_center/network_report.hpp"

#include "nexus/core/time.hpp"
#include "nexus/module/network_center/network_repository.hpp"
#include "nexus/services/report_format.hpp"

#include <string>

namespace nexus::module::network_center {

namespace {

using nexus::services::ReportFormat;
using nexus::services::report::csv_cell;
using nexus::services::report::html_document;
using nexus::services::report::html_escape;
using nexus::services::report::status_class;

std::string render_csv(NetworkRepository& repo) {
    std::string out = "network,address,hostname,label,status,last_seen_at\n";
    for (const NetworkRange& network : repo.networks()) {
        for (const Device& device : repo.devices(network.id)) {
            out += csv_cell(network.label.empty() ? network.cidr : network.label) + "," +
                   csv_cell(device.address) + "," + csv_cell(device.hostname) + "," +
                   csv_cell(device.label) + "," + csv_cell(device.status) + "," +
                   csv_cell(nexus::core::to_iso8601(device.last_seen_at)) + "\n";
        }
    }
    return out;
}

std::string render_html(NetworkRepository& repo) {
    std::string body;
    for (const NetworkRange& network : repo.networks()) {
        const auto devices = repo.devices(network.id);
        int online = 0;
        for (const Device& device : devices) {
            if (device.online()) {
                ++online;
            }
        }
        body += "<h2>" + html_escape(network.label.empty() ? network.cidr : network.label) + " (" +
                html_escape(network.cidr) + ")</h2><p class=\"muted\">" + std::to_string(online) +
                " of " + std::to_string(devices.size()) +
                " device(s) online.</p>"
                "<table>"
                "<tr><th>Address</th><th>Hostname</th><th>Label</th><th>Status</th>"
                "<th>Last seen</th></tr>";
        for (const Device& device : devices) {
            body += "<tr><td>" + html_escape(device.address) + "</td><td>" +
                    html_escape(device.hostname) + "</td><td>" + html_escape(device.label) +
                    "</td><td><span class=\"" + std::string(status_class(device.status)) + "\">" +
                    html_escape(device.status) + "</span></td><td>" +
                    html_escape(nexus::core::to_iso8601(device.last_seen_at)) + "</td></tr>";
        }
        body += "</table>";
    }
    return html_document("Network", body);
}

} // namespace

std::string render_network_report(NetworkRepository& repo, ReportFormat format) {
    return format == ReportFormat::Csv ? render_csv(repo) : render_html(repo);
}

} // namespace nexus::module::network_center
