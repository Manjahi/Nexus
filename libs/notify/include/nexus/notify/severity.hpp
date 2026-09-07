#pragma once

#include <optional>
#include <string_view>

namespace nexus::notify {

enum class Severity {
    Info,
    Success,
    Warning,
    Error,
};

[[nodiscard]] std::string_view to_string(Severity severity) noexcept;
[[nodiscard]] std::optional<Severity> severity_from_string(std::string_view text) noexcept;

} // namespace nexus::notify
