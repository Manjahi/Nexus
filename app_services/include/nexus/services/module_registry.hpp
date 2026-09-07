#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace nexus::db {
class SettingsRepository;
}

namespace nexus::services {

struct ModuleInfo {
    std::string id;           ///< stable slug, e.g. "storage"
    std::string display_name; ///< user-facing label
    bool default_enabled = true;
};

/// Tracks which optional modules are enabled (UFR-002). State persists in
/// `app_settings` under `module.<id>.enabled`.
class ModuleRegistry {
public:
    ModuleRegistry(nexus::db::SettingsRepository& settings, std::vector<ModuleInfo> known);

    [[nodiscard]] const std::vector<ModuleInfo>& modules() const noexcept { return known_; }
    [[nodiscard]] bool is_known(std::string_view id) const;
    [[nodiscard]] bool is_enabled(std::string_view id) const;

    /// Persists the new state. No-op (returns false) for an unknown id or when
    /// the state is unchanged.
    bool set_enabled(std::string_view id, bool enabled);

private:
    static std::string setting_key(std::string_view id);

    nexus::db::SettingsRepository* settings_;
    std::vector<ModuleInfo> known_;
};

} // namespace nexus::services
