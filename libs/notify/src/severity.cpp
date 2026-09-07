#include "nexus/notify/severity.hpp"

namespace nexus::notify {

std::string_view to_string(Severity severity) noexcept {
    switch (severity) {
        case Severity::Info:
            return "info";
        case Severity::Success:
            return "success";
        case Severity::Warning:
            return "warning";
        case Severity::Error:
            return "error";
    }
    return "info";
}

std::optional<Severity> severity_from_string(std::string_view text) noexcept {
    if (text == "info") {
        return Severity::Info;
    }
    if (text == "success") {
        return Severity::Success;
    }
    if (text == "warning") {
        return Severity::Warning;
    }
    if (text == "error") {
        return Severity::Error;
    }
    return std::nullopt;
}

} // namespace nexus::notify
