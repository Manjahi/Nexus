#include "nexus/module/connectivity/prober.hpp"

#include <chrono>
#include <utility>
#include <vector>

#include "nexus/notify/notification_center.hpp"
#include "nexus/notify/severity.hpp"

namespace nexus::module::connectivity {

namespace {
constexpr int kPruneEveryTicks = 240;
}

Prober::Prober(std::unique_ptr<ConnectivityRepository> repository,
               nexus::notify::NotificationCenter& notifications, ProbeFn probe, int outage_after)
    : repository_(std::move(repository)),
      notifications_(&notifications),
      probe_(std::move(probe)),
      outage_after_(outage_after < 1 ? 1 : outage_after) {}

Prober::~Prober() = default;

void Prober::tick() {
    if (!active_.load(std::memory_order_relaxed) || !probe_) {
        return;
    }

    const auto targets = repository_->targets(/*enabled_only=*/true);
    const nexus::core::Timestamp now = nexus::core::now();

    std::vector<ConnectivitySample> samples;
    samples.reserve(targets.size());

    for (const ProbeTarget& target : targets) {
        const ProbeReading reading = probe_(target);

        ConnectivitySample sample;
        sample.target_id = target.id;
        sample.status = reading.status;
        sample.rtt = reading.rtt;
        sample.detail = reading.detail;
        samples.push_back(sample);

        const std::string label = target.label.empty() ? target.id : target.label;

        if (reading.ok()) {
            fail_streak_[target.id] = 0;
            const auto open = open_outage_.find(target.id);
            if (open != open_outage_.end()) {
                repository_->end_outage(open->second, now);
                open_outage_.erase(open);
                notifications_->post("connectivity", nexus::notify::Severity::Info,
                                     "Connectivity restored: " + label, {});
            }
        } else {
            const int streak = ++fail_streak_[target.id];
            const auto open = open_outage_.find(target.id);
            if (open != open_outage_.end()) {
                repository_->bump_outage(open->second);
            } else if (streak >= outage_after_) {
                const std::int64_t id = repository_->begin_outage(target.id, now);
                open_outage_[target.id] = id;
                notifications_->post("connectivity", nexus::notify::Severity::Warning,
                                     "Connectivity lost: " + label, reading.detail);
            }
        }
    }

    repository_->record_samples(samples, now);

    if (++ticks_ % kPruneEveryTicks == 0) {
        repository_->prune_before(now - std::chrono::hours{24 * 30});
    }
}

} // namespace nexus::module::connectivity
