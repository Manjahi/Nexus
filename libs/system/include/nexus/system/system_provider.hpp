#pragma once

#include "nexus/system/metrics.hpp"

#include <memory>
#include <vector>

namespace nexus::system {

/// Reads live machine metrics from the host OS.
///
/// Rate-based readings (`cpu_load`, per-process `cpu_fraction`) are computed
/// against the previous call, so a single caller should drive one provider on a
/// steady cadence. Implementations are NOT thread-safe.
class SystemProvider {
public:
    virtual ~SystemProvider() = default;

    virtual CpuLoad cpu_load() = 0;
    virtual MemoryStatus memory_status() = 0;
    virtual std::vector<DiskInfo> disks() = 0;
    virtual std::vector<NetInterfaceInfo> network_interfaces() = 0;
    virtual std::vector<ProcessInfo> processes() = 0;
    virtual BatteryStatus battery() = 0;
};

/// Builds the provider for the current platform.
[[nodiscard]] std::unique_ptr<SystemProvider> make_system_provider();

} // namespace nexus::system
