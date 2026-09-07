#pragma once

#include <chrono>
#include <string>

namespace nexus::core {

using Clock = std::chrono::system_clock;
using Timestamp = Clock::time_point;

[[nodiscard]] Timestamp now() noexcept;

/// Formats a timestamp as ISO-8601 UTC with second precision, e.g.
/// "2026-09-07T12:34:56Z".
[[nodiscard]] std::string to_iso8601(Timestamp tp);

} // namespace nexus::core
