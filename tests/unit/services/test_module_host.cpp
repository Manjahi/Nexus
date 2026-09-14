#include "nexus/services/module_host.hpp"

#include "nexus/db/database.hpp"
#include "nexus/db/migration.hpp"
#include "nexus/db/settings_repository.hpp"
#include "nexus/jobs/scheduler.hpp"
#include "nexus/jobs/thread_pool.hpp"
#include "nexus/notify/notification_center.hpp"
#include "nexus/services/audit_log.hpp"
#include "nexus/services/event_bus.hpp"
#include "nexus/services/job_repository.hpp"
#include "nexus/services/module.hpp"
#include "nexus/services/module_registry.hpp"
#include "nexus/services/notification_repository.hpp"
#include "nexus/services/report_center.hpp"
#include "nexus/services/service_context.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace nexus;

namespace {

struct Harness {
    db::Database db = db::Database::open_in_memory();
    db::SettingsRepository settings{db};
    jobs::ThreadPool pool{1};
    jobs::Scheduler scheduler{pool};
    notify::NotificationCenter notifications;
    services::EventBus events;
    services::AuditLog audit{db};
    services::ModuleRegistry modules;
    services::JobRepository jobsRepo{db};
    services::NotificationRepository notesRepo{db};
    services::ReportCenter reports{db, std::filesystem::temp_directory_path()};
    services::ServiceContext ctx;

    explicit Harness(std::vector<services::ModuleInfo> known)
        : modules(settings, std::move(known)),
          ctx{db,     settings, pool,     scheduler, notifications, events,
              audit,  modules,  jobsRepo, notesRepo, reports} {
        db::migrate(db, "core", db::core_migrations());
    }
};

struct Counters {
    int migrated = 0;
    int started = 0;
    int stopped = 0;
};

class FakeModule : public services::Module {
public:
    FakeModule(std::string id, Counters* counters) : id_(std::move(id)), counters_(counters) {}

    std::string_view id() const override { return id_; }
    void apply_migrations(db::Database&) override { ++counters_->migrated; }
    void start(services::ServiceContext&) override { ++counters_->started; }
    void stop() override { ++counters_->stopped; }

private:
    std::string id_;
    Counters* counters_;
};

enum class ThrowStage { Migrate, Start, Stop };

// A module that always misbehaves at one lifecycle stage - for exercising
// ModuleHost's UFR-020 crash isolation.
class ThrowingModule : public services::Module {
public:
    ThrowingModule(std::string id, ThrowStage stage) : id_(std::move(id)), stage_(stage) {}

    std::string_view id() const override { return id_; }

    void apply_migrations(db::Database&) override {
        if (stage_ == ThrowStage::Migrate) {
            throw std::runtime_error("boom in migrate");
        }
    }
    void start(services::ServiceContext&) override {
        ++started;
        if (stage_ == ThrowStage::Start) {
            throw std::runtime_error("boom in start");
        }
    }
    void stop() override {
        ++stopped;
        if (stage_ == ThrowStage::Stop) {
            throw std::runtime_error("boom in stop");
        }
    }

    int started = 0;
    int stopped = 0;

private:
    std::string id_;
    ThrowStage stage_;
};

} // namespace

TEST_CASE("module host migrates on add and starts only enabled modules", "[services][modulehost]") {
    Harness h({{"alpha", "Alpha", true}, {"beta", "Beta", false}});
    Counters alpha;
    Counters beta;

    services::ModuleHost host(h.ctx);
    host.add(std::make_unique<FakeModule>("alpha", &alpha));
    host.add(std::make_unique<FakeModule>("beta", &beta));

    REQUIRE(alpha.migrated == 1);
    REQUIRE(beta.migrated == 1);

    host.start_enabled();
    REQUIRE(alpha.started == 1);
    REQUIRE(beta.started == 0);
    REQUIRE(host.is_running("alpha"));
    REQUIRE_FALSE(host.is_running("beta"));
    REQUIRE(host.running() == std::vector<std::string>{"alpha"});

    SECTION("start_enabled is idempotent") {
        host.start_enabled();
        REQUIRE(alpha.started == 1);
    }

    SECTION("enabling beta then re-running starts it too") {
        h.modules.set_enabled("beta", true);
        host.start_enabled();
        REQUIRE(beta.started == 1);
        REQUIRE(alpha.started == 1);
    }

    SECTION("stop_all stops running modules once") {
        host.stop_all();
        REQUIRE(alpha.stopped == 1);
        REQUIRE(beta.stopped == 0);
        REQUIRE(host.running().empty());
        host.stop_all();
        REQUIRE(alpha.stopped == 1);
    }
}

TEST_CASE("module host destructor stops running modules", "[services][modulehost]") {
    Harness h({{"alpha", "Alpha", true}});
    Counters counters;
    {
        services::ModuleHost host(h.ctx);
        host.add(std::make_unique<FakeModule>("alpha", &counters));
        host.start_enabled();
        REQUIRE(counters.started == 1);
    }
    REQUIRE(counters.stopped == 1);
}

TEST_CASE("a migration failure degrades only that module (UFR-020)",
         "[services][modulehost][crash-isolation]") {
    Harness h({{"bad", "Bad", true}, {"good", "Good", true}});
    Counters good;

    services::ModuleHost host(h.ctx);
    host.add(std::make_unique<ThrowingModule>("bad", ThrowStage::Migrate));
    host.add(std::make_unique<FakeModule>("good", &good));

    REQUIRE(host.is_degraded("bad"));
    REQUIRE_FALSE(host.is_degraded("good"));
    REQUIRE(good.migrated == 1);

    host.start_enabled();
    REQUIRE_FALSE(host.is_running("bad")); // never even attempted: schema may be partial
    REQUIRE(host.is_running("good"));
    REQUIRE(good.started == 1);

    REQUIRE(host.failures().size() == 1);
    REQUIRE(host.failures()[0].id == "bad");
    REQUIRE(host.failures()[0].stage == "migrate");
    REQUIRE(h.notifications.size() == 1);
    REQUIRE(h.audit.recent().size() == 1);
}

TEST_CASE("a start failure degrades only that module and the rest still start",
         "[services][modulehost][crash-isolation]") {
    Harness h({{"bad", "Bad", true}, {"good", "Good", true}});
    Counters good;

    services::ModuleHost host(h.ctx);
    host.add(std::make_unique<ThrowingModule>("bad", ThrowStage::Start));
    host.add(std::make_unique<FakeModule>("good", &good));

    host.start_enabled();

    REQUIRE(host.is_degraded("bad"));
    REQUIRE_FALSE(host.is_running("bad"));
    REQUIRE(host.is_running("good"));
    REQUIRE(good.started == 1);
    REQUIRE(host.failures().size() == 1);
    REQUIRE(host.failures()[0].stage == "start");
}

TEST_CASE("a stop failure is isolated so every module still gets stopped",
         "[services][modulehost][crash-isolation]") {
    Harness h({{"bad", "Bad", true}, {"good", "Good", true}});
    Counters good;

    services::ModuleHost host(h.ctx);
    auto bad = std::make_unique<ThrowingModule>("bad", ThrowStage::Stop);
    ThrowingModule* bad_ptr = bad.get();
    host.add(std::move(bad));
    host.add(std::make_unique<FakeModule>("good", &good));
    host.start_enabled();

    host.stop_all();

    REQUIRE(bad_ptr->stopped == 1); // stop() was still called...
    REQUIRE(good.stopped == 1);     // ...and 'good' wasn't skipped because 'bad' threw
    REQUIRE(host.running().empty());
    REQUIRE(host.is_degraded("bad"));
    REQUIRE(host.failures().back().stage == "stop");
}
