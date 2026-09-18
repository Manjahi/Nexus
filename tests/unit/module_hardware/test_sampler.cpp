#include "nexus/module/hardware/sampler.hpp"

#include "nexus/db/database.hpp"
#include "nexus/db/migration.hpp"
#include "nexus/module/hardware/hardware_repository.hpp"
#include "nexus/notify/notification_center.hpp"
#include "nexus/system/system_provider.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <memory>
#include <vector>

using namespace nexus::module::hardware;

namespace {

class FakeProvider : public nexus::system::SystemProvider {
public:
    double cpu_total = 0.2;

    nexus::system::CpuLoad cpu_load() override {
        return {cpu_total, {cpu_total, cpu_total}};
    }
    nexus::system::MemoryStatus memory_status() override {
        nexus::system::MemoryStatus m;
        m.total_bytes = 16'000'000'000ULL;
        m.available_bytes = 8'000'000'000ULL;
        m.used_bytes = 8'000'000'000ULL;
        m.used_fraction = 0.5;
        return m;
    }
    std::vector<nexus::system::DiskInfo> disks() override {
        nexus::system::DiskInfo d;
        d.mount = "C:\\";
        d.total_bytes = 1000;
        d.free_bytes = 400;
        return {d};
    }
    std::vector<nexus::system::NetInterfaceInfo> network_interfaces() override { return {}; }
    std::vector<nexus::system::ProcessInfo> processes() override {
        nexus::system::ProcessInfo p;
        p.pid = 42;
        p.name = "fake.exe";
        p.cpu_fraction = 0.3;
        p.working_set_bytes = 123;
        return {p};
    }
    nexus::system::BatteryStatus battery() override { return {}; }
};

nexus::db::Database migrated_db() {
    auto db = nexus::db::Database::open_in_memory();
    nexus::db::migrate(db, "hardware", hardware_migrations());
    return db;
}

} // namespace

TEST_CASE("tick persists metric and process samples", "[hardware][sampler]") {
    auto db = migrated_db();
    auto fake = std::make_unique<FakeProvider>();
    nexus::notify::NotificationCenter notifications;

    Sampler sampler(std::move(fake), std::make_unique<HardwareRepository>(db), notifications);
    sampler.tick();

    HardwareRepository repo(db);
    const auto since = nexus::core::now() - std::chrono::minutes{1};
    REQUIRE(repo.metric_series("cpu.total", "", since).size() == 1);
    REQUIRE(repo.metric_series("disk.free_fraction", "C:\\", since).size() == 1);
    REQUIRE(repo.metric_series("disk.free_fraction", "C:\\", since)[0].value == 0.4);
    REQUIRE(repo.latest_processes().size() == 1);
    REQUIRE(repo.latest_processes()[0].name == "fake.exe");
    REQUIRE(sampler.ticks() == 1);
}

TEST_CASE("threshold breach notifies once, then again after recovery", "[hardware][sampler]") {
    auto db = migrated_db();
    auto fake = std::make_unique<FakeProvider>();
    FakeProvider* fake_ptr = fake.get();
    nexus::notify::NotificationCenter notifications;

    Sampler sampler(std::move(fake), std::make_unique<HardwareRepository>(db), notifications);

    fake_ptr->cpu_total = 0.10; // below the 0.95 default threshold
    sampler.tick();
    REQUIRE(notifications.size() == 0);

    fake_ptr->cpu_total = 0.99; // breach
    sampler.tick();
    sampler.tick(); // sustained breach: no repeat
    REQUIRE(notifications.size() == 1);
    REQUIRE(notifications.recent()[0].severity == nexus::notify::Severity::Warning);

    fake_ptr->cpu_total = 0.20; // recover
    sampler.tick();
    REQUIRE(notifications.size() == 2);
    REQUIRE(notifications.recent()[0].severity == nexus::notify::Severity::Info);
}

TEST_CASE("an inactive sampler does nothing", "[hardware][sampler]") {
    auto db = migrated_db();
    nexus::notify::NotificationCenter notifications;
    Sampler sampler(std::make_unique<FakeProvider>(), std::make_unique<HardwareRepository>(db),
                    notifications);

    sampler.set_active(false);
    sampler.tick();

    HardwareRepository repo(db);
    REQUIRE(repo.metric_series("cpu.total", "", nexus::core::now() - std::chrono::minutes{1}).empty());
    REQUIRE(sampler.ticks() == 0);
}

// UFR-010: retention is threaded from the caller into Sampler and actually
// gates what prune_before() removes - closes the "Known gaps" note in
// docs/UFR_CONFORMANCE.md about the settings value reaching a real prune.
TEST_CASE("sampler prunes samples older than its configured retention",
         "[hardware][sampler][retention]") {
    auto db = migrated_db();
    HardwareRepository repo(db);

    const auto now = nexus::core::now();
    // Seeded directly (bypassing tick(), which always stamps "now") so it
    // predates the sampler's own inserts and falls outside a 24h retention.
    const std::vector<MetricSample> old_sample{{"cpu.total", "", 0.9}};
    repo.record_metrics(old_sample, now - std::chrono::hours{48});

    nexus::notify::NotificationCenter notifications;
    Sampler sampler(std::make_unique<FakeProvider>(), std::make_unique<HardwareRepository>(db),
                    notifications, std::chrono::hours{24});

    // sampler.cpp's kPruneEveryTicks is 120 and not exposed - tick comfortably past it.
    for (int i = 0; i < 121; ++i) {
        sampler.tick();
    }

    const auto series = repo.metric_series("cpu.total", "", now - std::chrono::hours{72});
    REQUIRE_FALSE(series.empty()); // the sampler's own recent ticks remain
    REQUIRE(std::none_of(series.begin(), series.end(), [&](const auto& point) {
        return point.at < now - std::chrono::hours{24};
    }));
}
