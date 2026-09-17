#pragma once

// Small text helpers shared by report renderers.

#include <array>
#include <cstdio>
#include <string>
#include <string_view>

namespace nexus::services::report {

inline std::string number(double value, int precision = 3) {
    std::array<char, 64> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%.*f", precision, value);
    return std::string(buffer.data());
}

inline std::string html_escape(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c; break;
        }
    }
    return out;
}

inline std::string csv_cell(std::string_view text) {
    if (text.find_first_of(",\"\r\n") == std::string_view::npos) {
        return std::string(text);
    }
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"') {
            out += '"';
        }
        out += c;
    }
    out += '"';
    return out;
}

// Shared visual theme (docs/BRANDING.md's palette) for every generated HTML
// report, so System diagnostic/Storage cleanup/Backup/Network/Internet
// reliability look like one product instead of each hand-rolling
// <table border="1">. Callers pass just their body content (h2 sections,
// <table>s built with plain <th>/<td> - no border/cellpadding attributes).
inline std::string html_document(std::string_view title, std::string_view body_html) {
    static constexpr std::string_view kStyle = R"css(
:root {
  --nx-midnight: #0b1f33;
  --nx-cyan: #25b7d3;
  --nx-action: #176b87;
  --nx-background: #f4f7fa;
  --nx-surface: #ffffff;
  --nx-text: #162633;
  --nx-text-muted: #5d6f7e;
  --nx-border: #d5e0e7;
  --nx-selection: #e8f7fa;
  --nx-row-alternate: #f8fafc;
  --nx-success: #147d64;
  --nx-success-soft: #e7f6f0;
  --nx-info: #176b87;
  --nx-info-soft: #e6f4f8;
  --nx-warning: #b76e00;
  --nx-warning-soft: #fff4d8;
  --nx-critical: #c33d4b;
  --nx-critical-soft: #fdecee;
}
body { margin: 0; padding: 32px; background: var(--nx-background); color: var(--nx-text);
  font-family: Inter, "Segoe UI", Arial, sans-serif; line-height: 1.5; }
.report { max-width: 1120px; margin: 0 auto; background: var(--nx-surface);
  border: 1px solid var(--nx-border); border-radius: 12px; overflow: hidden; }
.report-header { padding: 24px 32px; color: #ffffff; background: var(--nx-midnight);
  border-bottom: 4px solid var(--nx-cyan); }
.report-header h1 { margin: 0; color: #ffffff; font-size: 1.5rem; }
.report-content { padding: 32px; }
h2, h3 { color: var(--nx-midnight); }
p.muted { color: var(--nx-text-muted); }
table { width: 100%; border-collapse: collapse; margin-bottom: 24px; }
th { padding: 12px; color: var(--nx-midnight); background: var(--nx-selection);
  border-bottom: 2px solid var(--nx-action); text-align: left; }
td { padding: 11px 12px; border-bottom: 1px solid var(--nx-border); }
tbody tr:nth-child(even) { background: var(--nx-row-alternate); }
.status-success { color: var(--nx-success); background: var(--nx-success-soft); padding: 2px 8px; border-radius: 4px; }
.status-warning { color: var(--nx-warning); background: var(--nx-warning-soft); padding: 2px 8px; border-radius: 4px; }
.status-critical { color: var(--nx-critical); background: var(--nx-critical-soft); padding: 2px 8px; border-radius: 4px; }
.status-info { color: var(--nx-info); background: var(--nx-info-soft); padding: 2px 8px; border-radius: 4px; }
@media print {
  body { padding: 0; background: #ffffff; }
  .report { max-width: none; border: 0; border-radius: 0; }
  * { print-color-adjust: exact; -webkit-print-color-adjust: exact; }
}
)css";

    std::string out;
    out += "<!doctype html><html><head><meta charset=\"utf-8\"><title>";
    out += html_escape(title);
    out += "</title><style>";
    out += kStyle;
    out += "</style></head><body><div class=\"report\"><div class=\"report-header\"><h1>";
    out += html_escape(title);
    out += "</h1></div><div class=\"report-content\">";
    out += body_html;
    out += "</div></div></body></html>";
    return out;
}

/// Best-effort mapping from a free-text status/state word to one of
/// html_document()'s semantic classes - callers with their own vocabulary
/// can just use the literal "status-success" etc. class names instead.
inline std::string_view status_class(std::string_view status) {
    if (status == "completed" || status == "ok" || status == "success" || status == "healthy" ||
        status == "online" || status == "recovered") {
        return "status-success";
    }
    if (status == "warning" || status == "degraded" || status == "approaching") {
        return "status-warning";
    }
    if (status == "failed" || status == "error" || status == "critical" || status == "offline" ||
        status == "breached") {
        return "status-critical";
    }
    return "status-info";
}

} // namespace nexus::services::report
