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

} // namespace nexus::services::report
