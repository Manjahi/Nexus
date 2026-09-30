#include "nexus/db/database.hpp"
#include "nexus/db/migration.hpp"
#include "nexus/module/network_center/network_repository.hpp"
#include "nexus/module/network_center/network_scanner.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

using namespace nexus::module::network_center;

namespace {

nexus::db::Database migrated_db() {
    auto db = nexus::db::Database::open_in_memory();
    nexus::db::migrate(db, "network_center", network_center_migrations());
    return db;
}

// A scriptable ping: "online" for every address except those in `down`.
// NetworkScanner previously hardcoded nexus::net::icmp_ping directly (no
// injection point, unlike DeviceMonitor's PingFn), making it untestable
// without real network I/O - this is the fake that closes that gap.
struct ScriptedPing {
    std::shared_ptr<std::unordered_set<std::string>> down =
        std::make_shared<std::unordered_set<std::string>>();

    nexus::net::PingResult operator()(std::string_view address) const {
        nexus::net::PingResult result;
        if (down->contains(std::string(address))) {
            result.status = nexus::net::ProbeStatus::Timeout;
        } else {
            result.status = nexus::net::ProbeStatus::Ok;
            result.rtt = std::chrono::microseconds{1000};
        }
        return result;
    }
};

// A scriptable port check: returns a fixed port list for every address
// (default: none open). Also closes an injection gap - default_port_check
// does real TCP connects, which the constructor's default argument would
// otherwise pull into every test below that only injects a fake ping.
struct ScriptedPortCheck {
    std::vector<std::uint16_t> open;

    std::vector<std::uint16_t> operator()(std::string_view) const { return open; }
};

// A scriptable ARP resolver: returns a fixed MAC for every address (default:
// unresolved). Same rationale as ScriptedPortCheck - default_arp_resolve
// does a real SendARP call, which every test below must avoid.
struct ScriptedArp {
    std::optional<std::string> mac;

    std::optional<std::string> operator()(std::string_view) const { return mac; }
};

} // namespace

TEST_CASE("scan upserts a device for every responding host", "[network_center][scanner]") {
    auto db = migrated_db();
    NetworkRepository repo(db);
    const auto network_id = repo.add_network("192.168.1.0/30", "Test range");

    ScriptedPing ping; // everyone responds
    NetworkScanner scanner(repo, ping, ScriptedPortCheck{}, ScriptedArp{});

    const auto summary = scanner.scan(network_id, "192.168.1.0/30");
    REQUIRE(summary.hosts_probed == 2); // /30 has 2 usable hosts
    REQUIRE(summary.devices_found == 2);
    REQUIRE_FALSE(summary.cancelled);

    const auto devices = repo.devices(network_id);
    REQUIRE(devices.size() == 2);
}

TEST_CASE("scan skips hosts that don't respond", "[network_center][scanner]") {
    auto db = migrated_db();
    NetworkRepository repo(db);
    const auto network_id = repo.add_network("192.168.1.0/30", "Test range");

    ScriptedPing ping;
    ping.down->insert("192.168.1.1");
    ping.down->insert("192.168.1.2");
    NetworkScanner scanner(repo, ping, ScriptedPortCheck{}, ScriptedArp{});

    const auto summary = scanner.scan(network_id, "192.168.1.0/30");
    REQUIRE(summary.hosts_probed == 2);
    REQUIRE(summary.devices_found == 0);
    REQUIRE(repo.devices(network_id).empty());
}

TEST_CASE("scan reports progress for every host probed", "[network_center][scanner]") {
    auto db = migrated_db();
    NetworkRepository repo(db);
    const auto network_id = repo.add_network("192.168.1.0/30", "Test range");

    ScriptedPing ping;
    NetworkScanner scanner(repo, ping, ScriptedPortCheck{}, ScriptedArp{});

    std::vector<ScanProgress> updates;
    const auto summary =
        scanner.scan(network_id, "192.168.1.0/30",
                     [&updates](ScanProgress progress) { updates.push_back(progress); });
    (void) summary;

    REQUIRE(updates.size() == 2);
    REQUIRE(updates.back().scanned == 2);
    REQUIRE(updates.back().total == 2);
}

TEST_CASE("scan stops early when cancelled", "[network_center][scanner]") {
    auto db = migrated_db();
    NetworkRepository repo(db);
    const auto network_id = repo.add_network("192.168.1.0/30", "Test range");

    ScriptedPing ping;
    NetworkScanner scanner(repo, ping, ScriptedPortCheck{}, ScriptedArp{});

    const auto summary = scanner.scan(network_id, "192.168.1.0/30", {}, [] { return true; });
    REQUIRE(summary.cancelled);
    REQUIRE(summary.hosts_probed == 0);
}

TEST_CASE("scan records open ports for responding hosts", "[network_center][scanner]") {
    auto db = migrated_db();
    NetworkRepository repo(db);
    const auto network_id = repo.add_network("192.168.1.0/30", "Test range");

    ScriptedPing ping;
    ScriptedPortCheck port_check;
    port_check.open = {80, 443};
    NetworkScanner scanner(repo, ping, port_check, ScriptedArp{});

    (void) scanner.scan(network_id, "192.168.1.0/30");

    const auto devices = repo.devices(network_id);
    REQUIRE(devices.size() == 2);
    for (const auto& device : devices) {
        REQUIRE(device.open_ports == "80,443");
    }
}

TEST_CASE("scan records an empty open-ports string when nothing is open",
          "[network_center][scanner]") {
    auto db = migrated_db();
    NetworkRepository repo(db);
    const auto network_id = repo.add_network("192.168.1.0/30", "Test range");

    ScriptedPing ping;
    NetworkScanner scanner(repo, ping, ScriptedPortCheck{}, ScriptedArp{}); // open = {}

    (void) scanner.scan(network_id, "192.168.1.0/30");

    const auto devices = repo.devices(network_id);
    REQUIRE(devices.size() == 2);
    for (const auto& device : devices) {
        REQUIRE(device.open_ports.empty());
    }
}

TEST_CASE("scan on an unparsable CIDR probes nothing", "[network_center][scanner]") {
    auto db = migrated_db();
    NetworkRepository repo(db);
    const auto network_id = repo.add_network("not-a-cidr", "Bad range");

    ScriptedPing ping;
    NetworkScanner scanner(repo, ping, ScriptedPortCheck{}, ScriptedArp{});

    const auto summary = scanner.scan(network_id, "not-a-cidr");
    REQUIRE(summary.hosts_probed == 0);
    REQUIRE(summary.devices_found == 0);
}

TEST_CASE("scan records a resolved MAC for responding hosts", "[network_center][scanner]") {
    auto db = migrated_db();
    NetworkRepository repo(db);
    const auto network_id = repo.add_network("192.168.1.0/30", "Test range");

    ScriptedPing ping;
    ScriptedArp arp;
    arp.mac = "AA:BB:CC:DD:EE:FF";
    NetworkScanner scanner(repo, ping, ScriptedPortCheck{}, arp);

    (void) scanner.scan(network_id, "192.168.1.0/30");

    const auto devices = repo.devices(network_id);
    REQUIRE(devices.size() == 2);
    for (const auto& device : devices) {
        REQUIRE(device.mac == "AA:BB:CC:DD:EE:FF");
    }
}

TEST_CASE("scan records an empty MAC when ARP resolution fails", "[network_center][scanner]") {
    auto db = migrated_db();
    NetworkRepository repo(db);
    const auto network_id = repo.add_network("192.168.1.0/30", "Test range");

    ScriptedPing ping;
    NetworkScanner scanner(repo, ping, ScriptedPortCheck{}, ScriptedArp{}); // mac = nullopt

    (void) scanner.scan(network_id, "192.168.1.0/30");

    const auto devices = repo.devices(network_id);
    REQUIRE(devices.size() == 2);
    for (const auto& device : devices) {
        REQUIRE(device.mac.empty());
    }
}

TEST_CASE("scan never calls ARP for a host that doesn't respond to ping",
          "[network_center][scanner]") {
    auto db = migrated_db();
    NetworkRepository repo(db);
    const auto network_id = repo.add_network("192.168.1.0/30", "Test range");

    ScriptedPing ping;
    ping.down->insert("192.168.1.1");
    ping.down->insert("192.168.1.2");

    std::vector<std::string> arp_calls;
    NetworkScanner scanner(repo, ping, ScriptedPortCheck{}, [&arp_calls](std::string_view address) {
        arp_calls.emplace_back(address);
        return std::optional<std::string>{"AA:BB:CC:DD:EE:FF"};
    });

    (void) scanner.scan(network_id, "192.168.1.0/30");
    REQUIRE(arp_calls.empty());
}
