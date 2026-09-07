#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

namespace nexus::core {

using Clock = std::chrono::system_clock;
using Timestamp = Clock::time_point;

[[nodiscard]] Timestamp now() noexcept;

/// Formats a timestamp as ISO-8601 UTC with second precision, e.g.
/// "2026-09-07T12:34:56Z".
[[nodiscard]] std::string to_iso8601(Timestamp tp);

/// Parses the exact form produced by to_iso8601 ("YYYY-MM-DDTHH:MM:SSZ").
/// Returns nullopt on any deviation. Sub-second precision is not accepted.
[[nodiscard]] std::optional<Timestamp> from_iso8601(std::string_view text);

} // namespace nexus::core
