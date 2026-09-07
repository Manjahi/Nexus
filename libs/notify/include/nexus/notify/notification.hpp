#pragma once

#include <optional>
#include <string>

#include "nexus/core/id.hpp"
#include "nexus/core/time.hpp"
#include "nexus/notify/severity.hpp"

namespace nexus::notify {

/// A discrete, user-facing message published by a module. Mirrors the
/// `notifications` table in the shared schema.
struct Notification {
    nexus::core::Uuid id;
    std::string module;
    Severity severity = Severity::Info;
    std::string title;
    std::string body;
    nexus::core::Timestamp created_at{};
    std::optional<nexus::core::Timestamp> read_at;

    [[nodiscard]] bool is_read() const noexcept { return read_at.has_value(); }
};

} // namespace nexus::notify
