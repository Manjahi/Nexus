#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "nexus/module/hardware/hardware_repository.hpp"

namespace nexus::system {
class SystemProvider;
}
namespace nexus::notify {
class NotificationCenter;
}

namespace nexus::module::hardware {

/// One sampling worker: reads the SystemProvider, persists metric and process
/// samples, and raises threshold notifications (edge-triggered, so a sustained
/// breach notifies once). Owns its provider and repository so an in-flight
/// tick() stays valid after the module stops (callers hold it by shared_ptr).
class Sampler {
public:
    Sampler(std::unique_ptr<nexus::system::SystemProvider> provider,
            std::unique_ptr<HardwareRepository> repository,
            nexus::notify::NotificationCenter& notifications);
    ~Sampler();

    Sampler(const Sampler&) = delete;
    Sampler& operator=(const Sampler&) = delete;

    void tick();

    void set_active(bool active) noexcept { active_.store(active, std::memory_order_relaxed); }
    [[nodiscard]] bool active() const noexcept { return active_.load(std::memory_order_relaxed); }

    /// Sample count so far (for tests / diagnostics).
    [[nodiscard]] int ticks() const noexcept { return ticks_; }

private:
    void evaluate_thresholds(const std::vector<MetricSample>& samples);

    std::unique_ptr<nexus::system::SystemProvider> provider_;
    std::unique_ptr<HardwareRepository> repository_;
    nexus::notify::NotificationCenter* notifications_;

    std::atomic<bool> active_{true};
    std::unordered_map<std::string, bool> breached_;
    int ticks_ = 0;
};

} // namespace nexus::module::hardware
