// Integration tests (module + db + jobs, per the implementation plan's
// "continuous workstreams" - docs/IMPLEMENTATION_PLAN.md): unlike the
// per-lib/per-module unit tests, these wire real modules to a real
// Database, ThreadPool, and Scheduler through ModuleHost and drive them for
// real (waiting for an actual scheduled tick to fire, not calling tick()
// directly) - the things a unit test that calls a worker's tick() method
// in-process can't catch: does ModuleHost's registration/start/stop
// sequence actually work end-to-end against the real job system, does a
// module's background worker actually get invoked by a live Scheduler, and
// does its output actually flow through to ReportCenter and
// NotificationRepository on disk.
//
// Deliberately narrow in scope (two of the simplest modules, one real
// scheduled tick) rather than a broad slow suite - see
// docs/UFR_CONFORMANCE.md's "Known gaps" for what this does and doesn't
// cover yet.

#include "nexus/db/database.hpp"
#include "nexus/db/migration.hpp"
#include "nexus/db/settings_repository.hpp"
#include "nexus/jobs/scheduler.hpp"
#include "nexus/jobs/thread_pool.hpp"
#include "nexus/module/connectivity/connectivity_module.hpp"
#include "nexus/module/hardware/hardware_module.hpp"
#include "nexus/module/hardware/hardware_repository.hpp"
#include "nexus/notify/notification_center.hpp"
#include "nexus/services/audit_log.hpp"
#include "nexus/services/event_bus.hpp"
#include "nexus/services/heavy_job_guard.hpp"
#include "nexus/services/job_repository.hpp"
#include "nexus/services/module_host.hpp"
#include "nexus/services/module_registry.hpp"
#include "nexus/services/notification_repository.hpp"
#include "nexus/services/report_center.hpp"
#include "nexus/services/service_context.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <memory>
#include <thread>

using namespace nexus;

namespace {

// A full, real platform (no fakes/mocks) over an in-memory database - the
// same shape as apps/desktop/src/Platform, minus the Qt shell.
struct Harness {
    std::filesystem::path reports_dir;
    db::Database db = db::Database::open_in_memory();
    db::SettingsRepository settings{db};
    jobs::ThreadPool pool{2};
    jobs::Scheduler scheduler{pool};
    notify::NotificationCenter notifications;
    services::EventBus events;
    services::AuditLog audit{db};
    services::ModuleRegistry modules;
    services::JobRepository jobs_repo{db};
    services::NotificationRepository notifications_repo{db};
    services::ReportCenter reports;
    services::HeavyJobGuard heavy_jobs;
    services::ServiceContext ctx;
    services::ModuleHost module_host;

    Harness()
        : reports_dir(
              std::filesystem::temp_directory_path() /
              ("nexuspc_integration_" +
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))),
          modules(settings, {{"hardware", "System Health", true},
                             {"connectivity", "Connectivity Center", true}}),
          reports(db, reports_dir),
          ctx{db,    settings, pool,      scheduler,          notifications, events,
              audit, modules,  jobs_repo, notifications_repo, reports,       heavy_jobs},
          module_host(ctx) {
        db::migrate(db, "core", db::core_migrations());
        std::filesystem::create_directories(reports_dir);
        services::attach_persistence(notifications, notifications_repo);
    }

    ~Harness() {
        module_host.stop_all();
        scheduler.stop();
        // See the matching comment in apps/desktop/src/Platform.cpp: a tick
        // already handed to the pool isn't cancelled by the two calls above,
        // and pool is declared (so destroyed) before several services below
        // it use - wait_idle() forces it to finish now, not during pool's own
        // destructor after those services are already gone. This exact test
        // (the first thing in the codebase to wait long enough for a real
        // recurring tick to be in flight at shutdown) is what caught the bug
        // this fixes in Platform.cpp too.
        pool.wait_idle();
        std::error_code ec;
        std::filesystem::remove_all(reports_dir, ec);
    }
};

} // namespace

TEST_CASE("ModuleHost brings up real modules against a live scheduler and thread pool",
          "[integration][platform]") {
    Harness h;

    h.module_host.add(std::make_unique<module::hardware::HardwareModule>());
    h.module_host.add(std::make_unique<module::connectivity::ConnectivityModule>());
    REQUIRE(h.module_host.failures().empty()); // migrations for both succeeded

    h.module_host.start_enabled();
    REQUIRE(h.module_host.is_running("hardware"));
    REQUIRE(h.module_host.is_running("connectivity"));
    REQUIRE(h.module_host.failures().empty());

    SECTION("a real scheduled tick writes real data through the real Database") {
        module::hardware::HardwareRepository repo(h.db);
        // No REQUIRE-empty precondition here: schedule_every()'s default
        // initial_delay is zero, so the first tick can legitimately fire
        // between start_enabled() above and this line - a race, not a bug.

        // HardwareModule samples every 3s (see kSampleInterval in
        // hardware_module.cpp) - wait for one real tick from the live
        // Scheduler/ThreadPool, not a direct tick() call.
        bool sampled = false;
        for (int i = 0; i < 50 && !sampled; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds{200});
            sampled = !repo.latest_snapshot().empty();
        }
        REQUIRE(sampled);
    }

    SECTION("a module's report generator is reachable through the real ReportCenter") {
        const auto generators = h.ctx.reports.generators();
        const bool has_system_diagnostic =
            std::any_of(generators.begin(), generators.end(),
                        [](const services::ReportCenter::GeneratorInfo& g) {
                            return g.kind == "system-diagnostic";
                        });
        REQUIRE(has_system_diagnostic);

        const auto report =
            h.ctx.reports.generate("system-diagnostic", services::ReportFormat::Html);
        REQUIRE(std::filesystem::exists(report.path));
        REQUIRE(std::filesystem::file_size(report.path) > 0);
    }

    SECTION("stopping the host cleanly tears down both modules") {
        h.module_host.stop_all();
        REQUIRE(h.module_host.running().empty());
        REQUIRE(h.module_host.failures().empty()); // stop() didn't throw for either module
    }
}

TEST_CASE("a posted notification round-trips through the real NotificationRepository",
          "[integration][platform]") {
    Harness h;

    h.notifications.post("integration-test", notify::Severity::Info, "hello", "world");

    const auto persisted = h.notifications_repo.recent(10);
    REQUIRE(persisted.size() == 1);
    REQUIRE(persisted[0].title == "hello");
    REQUIRE(persisted[0].body == "world");
    REQUIRE(persisted[0].module == "integration-test");
}
