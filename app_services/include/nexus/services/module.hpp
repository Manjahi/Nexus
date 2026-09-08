#pragma once

#include <string_view>

namespace nexus::db {
class Database;
}

namespace nexus::services {

struct ServiceContext;

/// A suite module: an optional feature area (storage, hardware, connectivity,
/// ...) that owns some schema and some background work.
///
/// Lifecycle, driven by ModuleHost: `apply_migrations` once, then `start` if the
/// module is enabled, then `stop` on shutdown or when disabled. `stop` must be
/// safe to call when `start` never ran.
class Module {
public:
    virtual ~Module() = default;

    [[nodiscard]] virtual std::string_view id() const = 0;

    virtual void apply_migrations(nexus::db::Database& /*db*/) {}
    virtual void start(ServiceContext& ctx) = 0;
    virtual void stop() {}
};

} // namespace nexus::services
