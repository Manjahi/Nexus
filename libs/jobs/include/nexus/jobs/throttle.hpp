#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

namespace nexus::jobs {

/// How much a throttled job should yield the disk/CPU to the rest of the
/// system (UFR-017). Unlimited never sleeps; each step down trades job
/// throughput for system responsiveness.
enum class ThrottleLevel { Unlimited, High, Normal, Low };

[[nodiscard]] std::string_view to_string(ThrottleLevel level) noexcept;
[[nodiscard]] std::optional<ThrottleLevel> throttle_level_from_string(std::string_view text) noexcept;

/// A small, reusable pacing helper for resource-intensive job loops (storage
/// scan, backup). Call pace() once per unit of work (e.g. once per file); at
/// anything but Unlimited it sleeps briefly, capping how much of the
/// disk/CPU budget that job can claim. Not thread-safe - one instance per
/// job, matching the existing cancel-token-per-job pattern.
class Throttle {
public:
    explicit Throttle(ThrottleLevel level = ThrottleLevel::Unlimited) noexcept : level_(level) {}

    [[nodiscard]] ThrottleLevel level() const noexcept { return level_; }

    /// The fixed delay pace() sleeps for at `level` (zero at Unlimited).
    [[nodiscard]] static std::chrono::milliseconds delay_for(ThrottleLevel level) noexcept;

    void pace() const;

private:
    ThrottleLevel level_;
};

} // namespace nexus::jobs
