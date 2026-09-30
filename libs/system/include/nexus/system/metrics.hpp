#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace nexus::system {

/// Processor utilisation over the interval since the previous sample.
/// Fractions are in [0, 1]; the first sample after construction reads 0.
struct CpuLoad {
    double total = 0.0;
    std::vector<double> per_core;
};

struct MemoryStatus {
    std::uint64_t total_bytes = 0;
    std::uint64_t available_bytes = 0;
    std::uint64_t used_bytes = 0;
    double used_fraction = 0.0;
    std::uint64_t commit_limit_bytes = 0;
    std::uint64_t commit_used_bytes = 0;
};

struct DiskInfo {
    std::string mount; ///< e.g. "C:\\"
    std::string volume_label;
    std::uint64_t total_bytes = 0;
    std::uint64_t free_bytes = 0;
};

struct NetInterfaceInfo {
    std::string name; ///< friendly alias
    std::string description;
    bool up = false;
    std::uint64_t bytes_sent = 0; ///< cumulative since boot
    std::uint64_t bytes_received = 0;
    std::uint64_t link_speed_bps = 0;
};

struct ProcessInfo {
    std::uint32_t pid = 0;
    std::string name;
    std::uint64_t working_set_bytes = 0;
    double cpu_fraction = 0.0; ///< share of total CPU since the previous sample, [0, 1]
    std::uint32_t thread_count = 0;
};

struct BatteryStatus {
    bool present = false;
    bool charging = false;
    bool on_ac_power = false;
    double charge_fraction = 0.0; ///< [0, 1]; 0 when unknown
    std::optional<std::chrono::seconds> time_remaining;
};

} // namespace nexus::system
