#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "nexus/module/connectivity/connectivity_repository.hpp"
#include "nexus/services/event_bus.hpp"

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

/// Result of checking whether the default gateway (router) answers, used to
/// tell "your PC/network is offline" apart from "your router is fine but
/// the internet beyond it isn't" when every probe target is failing.
struct GatewayCheck {
    std::optional<std::string> gateway; ///< nullopt if no default route was found at all
    bool reachable = false;
};
using GatewayCheckFn = std::function<GatewayCheck()>;

/// Executes the enabled probe targets on each tick, records the results, and
/// maintains an outage per target (opened after `outage_after` consecutive
/// failures, closed on the next success). Notifies on outage open/close.
/// Also classifies a total outage (every target failing at once) as a local
/// vs. beyond-your-router problem by separately checking the gateway.
///
/// Owns its repository so an in-flight tick survives module stop.
class Prober {
public:
    /// `retention` (UFR-010): how far back samples/outages are kept - caller
    /// reads this from settings so it's configurable per install.
    /// `events`, if non-null, receives a ConnectivityStateEvent whenever the
    /// total-outage state changes (see classify_and_notify_total_outage) -
    /// the mechanism Backup (and, later, anything else) uses to pause work
    /// against network destinations without depending on Connectivity
    /// directly. Optional/nullable so the many existing tests that only care
    /// about notifications don't need an EventBus in scope.
    Prober(std::unique_ptr<ConnectivityRepository> repository,
           nexus::notify::NotificationCenter& notifications, ProbeFn probe, int outage_after = 2,
           std::chrono::hours retention = std::chrono::hours{24 * 30},
           GatewayCheckFn gateway_check = &Prober::default_gateway_check,
           nexus::services::EventBus* events = nullptr);
    ~Prober();

    Prober(const Prober&) = delete;
    Prober& operator=(const Prober&) = delete;

    void tick();

    void set_active(bool active) noexcept { active_.store(active, std::memory_order_relaxed); }
    [[nodiscard]] bool active() const noexcept { return active_.load(std::memory_order_relaxed); }
    [[nodiscard]] int ticks() const noexcept { return ticks_; }

    /// The probe function used by ConnectivityModule (dispatches to libnexus-net).
    [[nodiscard]] static ProbeReading default_probe(const ProbeTarget& target);

    /// The real gateway check used outside tests (nexus::net::default_gateway
    /// + an ICMP ping of it).
    [[nodiscard]] static GatewayCheck default_gateway_check();

private:
    void classify_and_notify_total_outage(nexus::core::Timestamp now);

    std::unique_ptr<ConnectivityRepository> repository_;
    nexus::notify::NotificationCenter* notifications_;
    ProbeFn probe_;
    int outage_after_;
    GatewayCheckFn gateway_check_;
    nexus::services::EventBus* events_;

    std::atomic<bool> active_{true};
    std::unordered_map<std::string, int> fail_streak_;
    std::unordered_map<std::string, std::int64_t> open_outage_;
    std::chrono::hours retention_;
    int ticks_ = 0;
    bool total_outage_notified_ = false; // edge-triggered, like the per-target outages above
};

} // namespace nexus::module::connectivity
