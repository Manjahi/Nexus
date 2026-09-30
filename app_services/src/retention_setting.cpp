#include "nexus/services/retention_setting.hpp"

#include "nexus/db/settings_repository.hpp"

#include <string>

namespace nexus::services {

std::chrono::hours retention_days_setting(nexus::db::SettingsRepository& settings,
                                          std::string_view key, int default_days) {
    const std::string raw = settings.get_or(key, std::to_string(default_days));
    int days = default_days;
    try {
        days = std::stoi(raw);
    } catch (...) {
        days = default_days;
    }
    return std::chrono::hours{24 * (days < 1 ? 1 : days)};
}

} // namespace nexus::services
