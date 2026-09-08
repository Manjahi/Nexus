#include "nexus/services/module_host.hpp"

#include <algorithm>
#include <utility>

#include "nexus/services/module.hpp"
#include "nexus/services/module_registry.hpp"
#include "nexus/services/service_context.hpp"

namespace nexus::services {

ModuleHost::~ModuleHost() {
    stop_all();
}

void ModuleHost::add(std::unique_ptr<Module> module) {
    if (module == nullptr) {
        return;
    }
    module->apply_migrations(ctx_->db);
    modules_.push_back(std::move(module));
}

void ModuleHost::start_enabled() {
    for (const std::unique_ptr<Module>& module : modules_) {
        if (is_running(module->id())) {
            continue;
        }
        if (!ctx_->modules.is_enabled(module->id())) {
            continue;
        }
        module->start(*ctx_);
        running_.push_back(module.get());
    }
}

void ModuleHost::stop_all() {
    for (auto it = running_.rbegin(); it != running_.rend(); ++it) {
        (*it)->stop();
    }
    running_.clear();
}

std::vector<std::string> ModuleHost::running() const {
    std::vector<std::string> ids;
    ids.reserve(running_.size());
    for (const Module* module : running_) {
        ids.emplace_back(module->id());
    }
    return ids;
}

bool ModuleHost::is_running(std::string_view id) const {
    return std::any_of(running_.begin(), running_.end(),
                       [&](const Module* module) { return module->id() == id; });
}

} // namespace nexus::services
