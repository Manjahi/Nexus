#pragma once

#include <optional>
#include <string>

namespace nexus::module::backup {

/// True if `destination` looks like a UNC network path (\\host\share\...).
/// A plain string-prefix check on the stored BackupJob::destination value,
/// not a std::filesystem::path predicate - path::root_name()/generic_string()
/// normalise separators in ways that make a UNC prefix check needlessly
/// fiddly, and every caller already has the raw string in hand.
[[nodiscard]] bool is_unc_destination(const std::string& destination) noexcept;

/// Checks that `destination` is reachable right now - meant to run once,
/// synchronously, when a user is about to save a new/edited backup job
/// (UFR-flavoured "fail fast, not three hours into the first scheduled
/// run"), not on every scheduled tick. Returns nullopt if reachable, or a
/// short human-readable reason if not: maps the common Win32 network error
/// codes (bad network path/name, logon failure, network unreachable,
/// access denied) to plain language instead of a raw error code, falling
/// back to std::error_code::message() for anything less common.
[[nodiscard]] std::optional<std::string> check_destination_reachable(
    const std::string& destination);

} // namespace nexus::module::backup
