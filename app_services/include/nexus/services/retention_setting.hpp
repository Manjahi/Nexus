#pragma once

#include <chrono>
#include <string_view>

namespace nexus::db {
class SettingsRepository;
}

namespace nexus::services {

/// UFR-010: reads a per-module retention window (in days) from app_settings,
/// falling back to `default_days` if the key is unset or unparsable, and
/// clamping to a minimum of one day. Shared by every module that prunes its
/// own history on a schedule (hardware, connectivity, network_center) so the
/// parsing rule - and its test coverage - lives in one place.
[[nodiscard]] std::chrono::hours retention_days_setting(nexus::db::SettingsRepository& settings,
                                                        std::string_view key, int default_days);

} // namespace nexus::services
