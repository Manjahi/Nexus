#include "nexus/system/system_provider.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <string>
#include <thread>

using nexus::system::make_system_provider;

namespace {
auto provider() {
    auto p = make_system_provider();
    REQUIRE(p != nullptr);
    return p;
}
} // namespace

TEST_CASE("memory status is internally consistent", "[system][memory]") {
    auto p = provider();
    const auto mem = p->memory_status();

    REQUIRE(mem.total_bytes > 0);
    REQUIRE(mem.available_bytes <= mem.total_bytes);
    REQUIRE(mem.used_bytes == mem.total_bytes - mem.available_bytes);
    REQUIRE(mem.used_fraction >= 0.0);
    REQUIRE(mem.used_fraction <= 1.0);
    REQUIRE(mem.commit_used_bytes <= mem.commit_limit_bytes);
}

TEST_CASE("at least one fixed disk is reported and totals are sane", "[system][disk]") {
    auto p = provider();
    const auto disks = p->disks();

    REQUIRE_FALSE(disks.empty());
    for (const auto& disk : disks) {
        REQUIRE_FALSE(disk.mount.empty());
        REQUIRE(disk.free_bytes <= disk.total_bytes);
        REQUIRE(disk.total_bytes > 0);
    }
}

TEST_CASE("cpu load is a fraction and stabilises after two samples", "[system][cpu]") {
    auto p = provider();
    (void)p->cpu_load(); // priming sample reads zero
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    const auto load = p->cpu_load();

    REQUIRE(load.total >= 0.0);
    REQUIRE(load.total <= 1.0);
    REQUIRE_FALSE(load.per_core.empty());
    REQUIRE(std::all_of(load.per_core.begin(), load.per_core.end(),
                        [](double v) { return v >= 0.0 && v <= 1.0; }));
}

TEST_CASE("process list includes this test process", "[system][process]") {
    auto p = provider();
    const auto processes = p->processes();

    REQUIRE(processes.size() > 5); // a live system always has many

    const auto named = std::count_if(processes.begin(), processes.end(),
                                     [](const auto& proc) { return !proc.name.empty(); });
    REQUIRE(named > static_cast<std::ptrdiff_t>(processes.size() / 2));

    const bool has_self = std::any_of(processes.begin(), processes.end(), [](const auto& proc) {
        return proc.name.find("nexus_system_tests") != std::string::npos;
    });
    REQUIRE(has_self);
}

TEST_CASE("network interfaces, when present, report sane fields", "[system][net]") {
    auto p = provider();
    for (const auto& iface : p->network_interfaces()) {
        INFO("interface: " << iface.name);
        REQUIRE_FALSE(iface.description.empty());
    }
}

TEST_CASE("battery status is self-consistent", "[system][battery]") {
    auto p = provider();
    const auto battery = p->battery();

    if (battery.present) {
        REQUIRE(battery.charge_fraction >= 0.0);
        REQUIRE(battery.charge_fraction <= 1.0);
    } else {
        REQUIRE(battery.charge_fraction == 0.0);
    }
}
