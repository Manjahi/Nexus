#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace nexus::services {

struct ServiceContext;
class Module;

/// Owns the registered modules and runs their lifecycle against a
/// ServiceContext. Not thread-safe; drive it from the UI thread at startup and
/// shutdown.
class ModuleHost {
public:
    explicit ModuleHost(ServiceContext& context) noexcept : ctx_(&context) {}
    ~ModuleHost();

    ModuleHost(const ModuleHost&) = delete;
    ModuleHost& operator=(const ModuleHost&) = delete;

    /// Registers a module and applies its migrations immediately.
    void add(std::unique_ptr<Module> module);

    /// Starts every registered module that the ModuleRegistry reports as
    /// enabled and is not already running.
    void start_enabled();

    /// Stops running modules in reverse start order.
    void stop_all();

    [[nodiscard]] std::vector<std::string> running() const;
    [[nodiscard]] bool is_running(std::string_view id) const;

private:
    ServiceContext* ctx_;
    std::vector<std::unique_ptr<Module>> modules_;
    std::vector<Module*> running_;
};

} // namespace nexus::services
