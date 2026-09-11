#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include "nexus/module/network_center/network_repository.hpp"

namespace nexus::notify {
class NotificationCenter;
}

namespace nexus::module::network_center {

struct PingReading {
    bool ok = false;
    std::optional<std::chrono::microseconds> rtt;
};

using PingFn = std::function<PingReading(const Device&)>;

/// Re-pings every known device on each tick, records a `check_results` row,
/// updates its status, and notifies on online/offline transitions. Never
/// discovers new addresses itself (that's NetworkScanner, run only when the
/// user asks) - it only follows up on devices already in the database.
///
/// Owns its repository so an in-flight tick survives module stop.
class DeviceMonitor {
public:
    DeviceMonitor(std::unique_ptr<NetworkRepository> repository,
                 nexus::notify::NotificationCenter& notifications, PingFn ping);
    ~DeviceMonitor();

    DeviceMonitor(const DeviceMonitor&) = delete;
    DeviceMonitor& operator=(const DeviceMonitor&) = delete;

    void tick();

    void set_active(bool active) noexcept { active_.store(active, std::memory_order_relaxed); }
    [[nodiscard]] bool active() const noexcept { return active_.load(std::memory_order_relaxed); }
    [[nodiscard]] int ticks() const noexcept { return ticks_; }

    /// The ping function used by NetworkCenterModule (dispatches to libnexus-net).
    [[nodiscard]] static PingReading default_ping(const Device& device);

private:
    std::unique_ptr<NetworkRepository> repository_;
    nexus::notify::NotificationCenter* notifications_;
    PingFn ping_;

    std::atomic<bool> active_{true};
    std::unordered_map<std::int64_t, std::string> last_status_;
    int ticks_ = 0;
};

} // namespace nexus::module::network_center
