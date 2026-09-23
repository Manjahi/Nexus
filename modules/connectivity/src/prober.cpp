#include "nexus/module/connectivity/prober.hpp"

#include <algorithm>
#include <chrono>
#include <utility>
#include <vector>

#include "nexus/net/probe.hpp"
#include "nexus/notify/notification_center.hpp"
#include "nexus/notify/severity.hpp"
#include "nexus/services/events/events.hpp"

namespace nexus::module::connectivity {

namespace {
constexpr int kPruneEveryTicks = 240;
constexpr std::chrono::milliseconds kGatewayPingTimeout{500};
}

GatewayCheck Prober::default_gateway_check() {
    GatewayCheck result;
    result.gateway = nexus::net::default_gateway();
    if (result.gateway) {
        result.reachable = nexus::net::icmp_ping(*result.gateway, kGatewayPingTimeout).ok();
    }
    return result;
}

Prober::Prober(std::unique_ptr<ConnectivityRepository> repository,
               nexus::notify::NotificationCenter& notifications, ProbeFn probe, int outage_after,
               std::chrono::hours retention, GatewayCheckFn gateway_check,
               nexus::services::EventBus* events)
    : repository_(std::move(repository)),
      notifications_(&notifications),
      probe_(std::move(probe)),
      outage_after_(outage_after < 1 ? 1 : outage_after),
      gateway_check_(std::move(gateway_check)),
      events_(events),
      retention_(retention) {}

Prober::~Prober() = default;

void Prober::classify_and_notify_total_outage(nexus::core::Timestamp now) {
    if (events_ != nullptr) {
        events_->publish(nexus::services::events::ConnectivityStateEvent{/*internet_reachable=*/false});
    }
    const GatewayCheck check = gateway_check_ ? gateway_check_() : GatewayCheck{};
    if (!check.gateway) {
        notifications_->post(
            "connectivity", nexus::notify::Severity::Warning, "Internet unreachable",
            "Every monitored target is failing and no network gateway could be found.");
        repository_->record_path_status(PathStatus::NoGatewayFound, now);
        return;
    }
    if (check.reachable) {
        notifications_->post("connectivity", nexus::notify::Severity::Warning,
                             "Internet unreachable (your router is fine)",
                             "Reached your router (" + *check.gateway +
                                 ") but nothing beyond it - likely an ISP or upstream issue.");
        repository_->record_path_status(PathStatus::BeyondRouter, now);
    } else {
        notifications_->post("connectivity", nexus::notify::Severity::Warning,
                             "Local network issue",
                             "Could not reach your router (" + *check.gateway +
                                 ") - check your Wi-Fi/cable connection.");
        repository_->record_path_status(PathStatus::LocalIssue, now);
    }
}

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

    // Distinguishes "your PC/network is offline" from "your router is fine
    // but nothing beyond it is" - only meaningful (and only checked) once
    // *every* target is failing at once, not on a single target's outage.
    if (!samples.empty()) {
        const bool all_failed = std::all_of(
            samples.begin(), samples.end(), [](const ConnectivitySample& s) { return !s.ok(); });
        if (all_failed && !total_outage_notified_) {
            classify_and_notify_total_outage(now);
            total_outage_notified_ = true;
        } else if (!all_failed) {
            if (total_outage_notified_) {
                notifications_->post("connectivity", nexus::notify::Severity::Info,
                                     "Connectivity restored", {});
                total_outage_notified_ = false;
                if (events_ != nullptr) {
                    events_->publish(nexus::services::events::ConnectivityStateEvent{
                        /*internet_reachable=*/true});
                }
            }
            // Not just on the specific recovery edge above - a connection
            // that's simply always been fine should read "all ok" in the
            // UI, not sit as "Unknown" forever for lack of ever having
            // recovered from anything.
            repository_->record_path_status(PathStatus::AllOk, now);
        }
    }

    if (++ticks_ % kPruneEveryTicks == 0) {
        repository_->prune_before(now - retention_);
    }
}

} // namespace nexus::module::connectivity
