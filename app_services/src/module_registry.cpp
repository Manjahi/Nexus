#include "nexus/services/module_registry.hpp"

#include <algorithm>
#include <utility>

#include "nexus/db/settings_repository.hpp"

namespace nexus::services {

ModuleRegistry::ModuleRegistry(nexus::db::SettingsRepository& settings, std::vector<ModuleInfo> known)
    : settings_(&settings), known_(std::move(known)) {}

std::string ModuleRegistry::setting_key(std::string_view id) {
    std::string key = "module.";
    key.append(id);
    key.append(".enabled");
    return key;
}

namespace {

const ModuleInfo* find_module(const std::vector<ModuleInfo>& modules, std::string_view id) {
    const auto it = std::find_if(modules.begin(), modules.end(),
                                 [&](const ModuleInfo& m) { return m.id == id; });
    return it == modules.end() ? nullptr : &*it;
}

} // namespace

bool ModuleRegistry::is_known(std::string_view id) const {
    return find_module(known_, id) != nullptr;
}

bool ModuleRegistry::is_enabled(std::string_view id) const {
    const ModuleInfo* info = find_module(known_, id);
    if (info == nullptr) {
        return false;
    }
    const std::string stored = settings_->get_or(setting_key(id), info->default_enabled ? "1" : "0");
    return stored == "1";
}

bool ModuleRegistry::set_enabled(std::string_view id, bool enabled) {
    if (!is_known(id)) {
        return false;
    }
    if (is_enabled(id) == enabled) {
        return false;
    }
    settings_->set(setting_key(id), enabled ? "1" : "0");
    return true;
}

} // namespace nexus::services
