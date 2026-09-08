#pragma once

#include <chrono>
#include <cstddef>
#include <vector>

#include "nexus/net/probe.hpp"

namespace nexus::net {

/// Summary of a run of latency samples.
struct LatencyStats {
    std::size_t sent = 0;
    std::size_t received = 0;
    double loss_fraction = 0.0;
    std::chrono::microseconds min{0};
    std::chrono::microseconds max{0};
    std::chrono::microseconds mean{0};
    /// Mean absolute difference between consecutive received RTTs.
    std::chrono::microseconds jitter{0};
};

/// Aggregates ping samples. Lost samples count toward `sent` and `loss_fraction`
/// only; timing stats are over received samples. Order is significant for
/// jitter.
[[nodiscard]] LatencyStats summarize(const std::vector<PingResult>& samples);

} // namespace nexus::net
