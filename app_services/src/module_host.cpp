#include "nexus/services/module_host.hpp"

#include "nexus/notify/notification_center.hpp"
#include "nexus/notify/severity.hpp"
#include "nexus/services/audit_log.hpp"
#include "nexus/services/module.hpp"
#include "nexus/services/module_registry.hpp"
#include "nexus/services/service_context.hpp"

#include <algorithm>
#include <exception>
#include <utility>

namespace nexus::services {

ModuleHost::ModuleHost(ServiceContext& context) noexcept : ctx_(&context) {
}

ModuleHost::~ModuleHost() {
    stop_all();
}

void ModuleHost::record_failure(std::string_view id, std::string_view stage, std::string message) {
    if (!is_degraded(id)) {
        degraded_.push_back(id);
    }
    failures_.push_back(ModuleFailure{id, stage, message});

    // Last line of defense (UFR-020): reporting a module failure must not
    // itself become an unhandled exception, even if the notification/audit
    // machinery is what's unhealthy.
    try {
        ctx_->notifications.post(std::string(id), nexus::notify::Severity::Error,
                                 "Module '" + std::string(id) + "' failed to " + std::string(stage),
                                 message);
        ctx_->audit.record("module_failure", std::string(id), std::string(stage) + ": " + message,
                           "system");
    } catch (...) {
        // Already recorded in failures_ above; that's the durable record if
        // the notification/audit path itself can't be reached.
    }
}

void ModuleHost::add(std::unique_ptr<Module> module) {
    if (module == nullptr) {
        return;
    }
    const std::string_view id = module->id();
    try {
        module->apply_migrations(ctx_->db);
    } catch (const std::exception& e) {
        record_failure(id, "migrate", e.what());
    } catch (...) {
        record_failure(id, "migrate", "unknown error");
    }
    modules_.push_back(std::move(module));
}

void ModuleHost::start_enabled() {
    for (const std::unique_ptr<Module>& module : modules_) {
        const std::string_view id = module->id();
        if (is_running(id) || is_degraded(id)) {
            continue;
        }
        if (!ctx_->modules.is_enabled(id)) {
            continue;
        }
        try {
            module->start(*ctx_);
            running_.push_back(module.get());
        } catch (const std::exception& e) {
            record_failure(id, "start", e.what());
        } catch (...) {
            record_failure(id, "start", "unknown error");
        }
    }
}

void ModuleHost::stop_all() {
    for (auto it = running_.rbegin(); it != running_.rend(); ++it) {
        const std::string_view id = (*it)->id();
        try {
            (*it)->stop();
        } catch (const std::exception& e) {
            record_failure(id, "stop", e.what());
        } catch (...) {
            record_failure(id, "stop", "unknown error");
        }
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

bool ModuleHost::is_degraded(std::string_view id) const {
    return std::find(degraded_.begin(), degraded_.end(), id) != degraded_.end();
}

} // namespace nexus::services
