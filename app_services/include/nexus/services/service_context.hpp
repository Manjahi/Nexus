#pragma once

namespace nexus::db {
class Database;
class SettingsRepository;
}
namespace nexus::jobs {
class ThreadPool;
class Scheduler;
}
namespace nexus::notify {
class NotificationCenter;
}

namespace nexus::services {

class EventBus;
class AuditLog;
class ModuleRegistry;

/// Non-owning bundle of the shared singletons a module needs. The application
/// owns each part and outlives every module that borrows the context.
struct ServiceContext {
    nexus::db::Database& db;
    nexus::db::SettingsRepository& settings;
    nexus::jobs::ThreadPool& pool;
    nexus::jobs::Scheduler& scheduler;
    nexus::notify::NotificationCenter& notifications;
    EventBus& events;
    AuditLog& audit;
    ModuleRegistry& modules;
};

} // namespace nexus::services
