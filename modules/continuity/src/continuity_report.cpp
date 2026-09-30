#include "nexus/module/continuity/continuity_report.hpp"

#include "nexus/core/time.hpp"
#include "nexus/module/continuity/continuity_readiness.hpp"
#include "nexus/module/continuity/continuity_repository.hpp"
#include "nexus/module/continuity/continuity_scenarios.hpp"
#include "nexus/services/report_format.hpp"

#include <algorithm>
#include <string>

namespace nexus::module::continuity {

namespace {

using nexus::services::ReportFormat;
using nexus::services::report::csv_cell;
using nexus::services::report::html_document;
using nexus::services::report::html_escape;
using nexus::services::report::status_class;

std::string render_csv(nexus::db::Database& db) {
    ContinuityRepository repo(db);
    const auto readiness = compute_readiness(db);
    const auto scenarios = evaluate_scenarios(db);
    const auto rehearsals = repo.recent_rehearsals(50);

    std::string out = "section,name,detail1,detail2\n";
    out += "summary,score," + std::to_string(readiness.score) + ",\n";
    out += "summary,tracked," + std::to_string(readiness.tracked_count) + ",\n";
    out += "summary,covered," + std::to_string(readiness.covered_count) + ",\n";
    out += "summary,verified," + std::to_string(readiness.verified_count) + ",\n";

    for (const auto& asset : readiness.assets) {
        out += "asset," + csv_cell(asset.label) + "," +
               (asset.covered ? "covered" : "not covered") + "," +
               (asset.verified ? "verified" : "not verified") + "\n";
    }
    for (const auto& scenario : scenarios) {
        out += "scenario," + csv_cell(std::string(scenario_name(scenario.kind))) + "," +
               (scenario.ready ? "ready" : "not ready") + ",\n";
    }
    for (const auto& r : rehearsals) {
        out += "rehearsal," + csv_cell(r.scenario) + "," + csv_cell(r.outcome) + "," +
               nexus::core::to_iso8601(r.started_at) + "\n";
    }
    return out;
}

std::string render_html(nexus::db::Database& db) {
    ContinuityRepository repo(db);
    const auto readiness = compute_readiness(db);
    const auto scenarios = evaluate_scenarios(db);
    const auto rehearsals = repo.recent_rehearsals(50);

    std::string body = "<h2>Summary</h2><p class=\"muted\">Recovery readiness score: " +
                       std::to_string(readiness.score) + "/100 - " +
                       std::to_string(readiness.covered_count) + " of " +
                       std::to_string(readiness.tracked_count) + " tracked asset(s) covered, " +
                       std::to_string(readiness.verified_count) + " verified.</p>";

    const auto assets = repo.list_assets();
    body += "<h2>Tracked Assets</h2>";
    if (assets.empty()) {
        body += "<p class=\"muted\">No assets tracked yet.</p>";
    } else {
        body += "<table><tr><th>Label</th><th>Kind</th><th>Covered</th><th>Verified</th></tr>";
        for (const auto& asset : assets) {
            const auto it = std::find_if(readiness.assets.begin(), readiness.assets.end(),
                                         [&](const auto& ar) { return ar.asset_id == asset.id; });
            const bool covered = it != readiness.assets.end() && it->covered;
            const bool verified = it != readiness.assets.end() && it->verified;
            body += "<tr><td>" + html_escape(asset.label) + "</td><td>" +
                    html_escape(to_string(asset.kind)) + "</td><td><span class=\"" +
                    std::string(status_class(covered ? "ok" : "failed")) + "\">" +
                    (covered ? "Covered" : "Not covered") + "</span></td><td><span class=\"" +
                    std::string(status_class(verified ? "ok" : "failed")) + "\">" +
                    (verified ? "Verified" : "Not verified") + "</span></td></tr>";
        }
        body += "</table>";
    }

    body += "<h2>Recovery Scenarios</h2><table><tr><th>Scenario</th><th>Status</th>"
            "<th>Checklist</th></tr>";
    for (const auto& scenario : scenarios) {
        std::string checklist;
        for (const auto& check : scenario.checks) {
            checklist += std::string(check.passed ? "&#10003; " : "&#10007; ") +
                         html_escape(check.description) + "<br>";
        }
        body += "<tr><td>" + html_escape(std::string(scenario_name(scenario.kind))) +
                "</td><td><span class=\"" +
                std::string(status_class(scenario.ready ? "ok" : "failed")) + "\">" +
                (scenario.ready ? "Ready" : "Not ready") + "</span></td><td>" + checklist +
                "</td></tr>";
    }
    body += "</table>";

    body += "<h2>Rehearsal History</h2>";
    if (rehearsals.empty()) {
        body += "<p class=\"muted\">No rehearsals run yet.</p>";
    } else {
        body += "<table><tr><th>Date</th><th>Scenario</th><th>Outcome</th><th>Detail</th></tr>";
        for (const auto& r : rehearsals) {
            body += "<tr><td>" + nexus::core::to_iso8601(r.started_at) + "</td><td>" +
                    html_escape(r.scenario) + "</td><td><span class=\"" +
                    std::string(status_class(r.outcome)) + "\">" + html_escape(r.outcome) +
                    "</span></td><td>" + html_escape(r.detail) + "</td></tr>";
        }
        body += "</table>";
    }

    return html_document("Recovery Plan", body);
}

} // namespace

std::string render_recovery_plan(nexus::db::Database& db, ReportFormat format) {
    return format == ReportFormat::Csv ? render_csv(db) : render_html(db);
}

} // namespace nexus::module::continuity
