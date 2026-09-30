#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace nexus::services {

struct ServiceContext;
class Module;

/// A module lifecycle stage that raised an exception (UFR-020: a module
/// failure must not crash unrelated modules or the app).
struct ModuleFailure {
    std::string_view id;    ///< the failing module's id() - a static string literal
    std::string_view stage; ///< "migrate" | "start" | "stop"
    std::string message;    ///< exception's what(), or "unknown error"
};

/// Owns the registered modules and runs their lifecycle against a
/// ServiceContext. Not thread-safe; drive it from the UI thread at startup and
/// shutdown.
///
/// Every lifecycle call into a module (apply_migrations/start/stop) is
/// exception-isolated: a throw there is caught, posted to the notification
/// center and audit log, and the module is marked degraded rather than
/// propagating and taking the rest of the app down with it.
class ModuleHost {
public:
    explicit ModuleHost(ServiceContext& context) noexcept;
    ~ModuleHost();

    ModuleHost(const ModuleHost&) = delete;
    ModuleHost& operator=(const ModuleHost&) = delete;

    /// Registers a module and applies its migrations immediately. A migration
    /// failure marks the module degraded (see failures()) and it is skipped
    /// by start_enabled() - starting against a possibly-partial schema would
    /// only compound the problem.
    void add(std::unique_ptr<Module> module);

    /// Starts every registered module that the ModuleRegistry reports as
    /// enabled, is not already running, and did not fail migration.
    void start_enabled();

    /// Stops running modules in reverse start order. A stop() failure is
    /// isolated the same way as migrate/start - the remaining modules still
    /// get their chance to stop cleanly.
    void stop_all();

    [[nodiscard]] std::vector<std::string> running() const;
    [[nodiscard]] bool is_running(std::string_view id) const;

    /// True once a module has failed any lifecycle stage.
    [[nodiscard]] bool is_degraded(std::string_view id) const;
    /// One record per failure, oldest first (a module can fail more than
    /// once, e.g. migrate then later stop).
    [[nodiscard]] const std::vector<ModuleFailure>& failures() const noexcept { return failures_; }

private:
    void record_failure(std::string_view id, std::string_view stage, std::string message);

    ServiceContext* ctx_;
    std::vector<std::unique_ptr<Module>> modules_;
    std::vector<Module*> running_;
    std::vector<std::string_view> degraded_;
    std::vector<ModuleFailure> failures_;
};

} // namespace nexus::services
