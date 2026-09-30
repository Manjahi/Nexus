#pragma once

#include "nexus/net/probe.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace nexus::module::connectivity {

class ConnectivityRepository;

using DownloadFn = std::function<nexus::net::HttpProbeResult(std::string_view url,
                                                             std::chrono::milliseconds timeout)>;

/// Runs a timed download against a configured URL and records the resulting
/// throughput. A trend over time, not a rigorous benchmark (server load,
/// route, and concurrent local traffic all affect a single measurement) -
/// spec's own framing for this feature.
class SpeedTester {
public:
    /// `download` defaults to a real HTTP GET (default_download); tests
    /// inject a fake to avoid depending on real network I/O, the same shape
    /// as NetworkScanner/DeviceMonitor/Prober's injectable probe functions.
    SpeedTester(std::unique_ptr<ConnectivityRepository> repository, std::string url,
                DownloadFn download = &SpeedTester::default_download);
    ~SpeedTester();

    SpeedTester(const SpeedTester&) = delete;
    SpeedTester& operator=(const SpeedTester&) = delete;

    void tick();

    void set_active(bool active) noexcept { active_.store(active, std::memory_order_relaxed); }
    [[nodiscard]] bool active() const noexcept { return active_.load(std::memory_order_relaxed); }
    [[nodiscard]] int ticks() const noexcept { return ticks_; }

    /// The real download used outside tests.
    [[nodiscard]] static nexus::net::HttpProbeResult
    default_download(std::string_view url, std::chrono::milliseconds timeout);

private:
    std::unique_ptr<ConnectivityRepository> repository_;
    std::string url_;
    DownloadFn download_;
    std::atomic<bool> active_{true};
    int ticks_ = 0;
};

} // namespace nexus::module::connectivity
