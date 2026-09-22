#include "nexus/module/connectivity/speed_test.hpp"

#include "nexus/db/database.hpp"
#include "nexus/db/migration.hpp"
#include "nexus/module/connectivity/connectivity_repository.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>

using namespace nexus::module::connectivity;
using namespace std::chrono_literals;

namespace {

nexus::db::Database migrated_db() {
    auto db = nexus::db::Database::open_in_memory();
    nexus::db::migrate(db, "connectivity", connectivity_migrations());
    return db;
}

// A scriptable download: returns a fixed byte count over a fixed elapsed
// time (bypassing real network I/O), or a failure when `fail` is set.
struct ScriptedDownload {
    std::uint64_t bytes = 1'250'000; // 10 Mbit at 1s
    std::chrono::microseconds elapsed{1'000'000};
    bool fail = false;

    nexus::net::HttpProbeResult operator()(std::string_view, std::chrono::milliseconds) const {
        nexus::net::HttpProbeResult result;
        if (fail) {
            result.status = nexus::net::ProbeStatus::Timeout;
            return result;
        }
        result.status = nexus::net::ProbeStatus::Ok;
        result.status_code = 200;
        result.bytes_received = bytes;
        result.elapsed = elapsed;
        return result;
    }
};

} // namespace

TEST_CASE("tick records download throughput on success", "[connectivity][speedtest]") {
    auto db = migrated_db();
    ScriptedDownload download;

    SpeedTester tester(std::make_unique<ConnectivityRepository>(db), "https://example.test/download",
                       download);
    tester.tick();
    REQUIRE(tester.ticks() == 1);

    ConnectivityRepository repo(db);
    const auto recent = repo.recent_speed_tests();
    REQUIRE(recent.size() == 1);
    REQUIRE(recent[0].download_bps.has_value());
    // 1,250,000 bytes * 8 bits / 1.0s = 10,000,000 bps.
    REQUIRE(recent[0].download_bps.value() == Catch::Approx(10'000'000.0));
    REQUIRE(recent[0].server == "https://example.test/download");
}

TEST_CASE("tick records a row with no throughput on failure", "[connectivity][speedtest]") {
    auto db = migrated_db();
    ScriptedDownload download;
    download.fail = true;

    SpeedTester tester(std::make_unique<ConnectivityRepository>(db), "https://example.test/download",
                       download);
    tester.tick();

    ConnectivityRepository repo(db);
    const auto recent = repo.recent_speed_tests();
    REQUIRE(recent.size() == 1);
    REQUIRE_FALSE(recent[0].download_bps.has_value());
}

TEST_CASE("an inactive speed tester does nothing", "[connectivity][speedtest]") {
    auto db = migrated_db();
    ScriptedDownload download;

    SpeedTester tester(std::make_unique<ConnectivityRepository>(db), "https://example.test/download",
                       download);
    tester.set_active(false);
    tester.tick();

    REQUIRE(tester.ticks() == 0);
    ConnectivityRepository repo(db);
    REQUIRE(repo.recent_speed_tests().empty());
}
