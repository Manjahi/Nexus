#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "nexus/module/connectivity/connectivity_repository.hpp"

namespace nexus::notify {
class NotificationCenter;
}

namespace nexus::module::connectivity {

struct ProbeReading {
    std::string status = "error"; ///< "ok" | "timeout" | "unreachable" | "dns_failure" | "error"
    std::optional<std::chrono::microseconds> rtt;
    std::string detail;

    [[nodiscard]] bool ok() const noexcept { return status == "ok"; }
};

using ProbeFn = std::function<ProbeReading(const ProbeTarget&)>;

/// Executes the enabled probe targets on each tick, records the results, and
/// maintains an outage per target (opened after `outage_after` consecutive
/// failures, closed on the next success). Notifies on outage open/close.
///
/// Owns its repository so an in-flight tick survives module stop.
class Prober {
public:
    Prober(std::unique_ptr<ConnectivityRepository> repository,
           nexus::notify::NotificationCenter& notifications, ProbeFn probe,
           int outage_after = 2);
    ~Prober();

    Prober(const Prober&) = delete;
    Prober& operator=(const Prober&) = delete;

    void tick();

    void set_active(bool active) noexcept { active_.store(active, std::memory_order_relaxed); }
    [[nodiscard]] bool active() const noexcept { return active_.load(std::memory_order_relaxed); }
    [[nodiscard]] int ticks() const noexcept { return ticks_; }

    /// The probe function used by ConnectivityModule (dispatches to libnexus-net).
    [[nodiscard]] static ProbeReading default_probe(const ProbeTarget& target);

private:
    std::unique_ptr<ConnectivityRepository> repository_;
    nexus::notify::NotificationCenter* notifications_;
    ProbeFn probe_;
    int outage_after_;

    std::atomic<bool> active_{true};
    std::unordered_map<std::string, int> fail_streak_;
    std::unordered_map<std::string, std::int64_t> open_outage_;
    int ticks_ = 0;
};

} // namespace nexus::module::connectivity
