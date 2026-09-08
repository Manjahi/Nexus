#include "nexus/net/latency_stats.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <numeric>

namespace nexus::net {

LatencyStats summarize(const std::vector<PingResult>& samples) {
    LatencyStats stats;
    stats.sent = samples.size();

    std::vector<std::chrono::microseconds> rtts;
    rtts.reserve(samples.size());
    for (const PingResult& sample : samples) {
        if (sample.ok()) {
            rtts.push_back(sample.rtt);
        }
    }

    stats.received = rtts.size();
    if (stats.sent > 0) {
        stats.loss_fraction =
            static_cast<double>(stats.sent - stats.received) / static_cast<double>(stats.sent);
    }
    if (rtts.empty()) {
        return stats;
    }

    const auto [min_it, max_it] = std::minmax_element(rtts.begin(), rtts.end());
    stats.min = *min_it;
    stats.max = *max_it;

    const std::int64_t total = std::accumulate(
        rtts.begin(), rtts.end(), std::int64_t{0},
        [](std::int64_t acc, std::chrono::microseconds v) { return acc + v.count(); });
    stats.mean = std::chrono::microseconds{total / static_cast<std::int64_t>(rtts.size())};

    if (rtts.size() >= 2) {
        std::int64_t jitter_sum = 0;
        for (std::size_t i = 1; i < rtts.size(); ++i) {
            jitter_sum += std::abs(rtts[i].count() - rtts[i - 1].count());
        }
        jitter_sum /= static_cast<std::int64_t>(rtts.size() - 1);
        stats.jitter = std::chrono::microseconds{jitter_sum};
    }

    return stats;
}

} // namespace nexus::net
