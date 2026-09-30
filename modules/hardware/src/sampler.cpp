#include "nexus/module/hardware/sampler.hpp"

#include "nexus/notify/notification_center.hpp"
#include "nexus/notify/severity.hpp"
#include "nexus/system/system_provider.hpp"

#include <algorithm>
#include <chrono>
#include <string>
#include <utility>

namespace nexus::module::hardware {

namespace {

constexpr std::size_t kProcessTopN = 25;
constexpr int kPruneEveryTicks = 120;

std::string key_of(std::string_view metric, std::string_view scope) {
    std::string key(metric);
    key.push_back('\x1f');
    key.append(scope);
    return key;
}

} // namespace

Sampler::Sampler(std::unique_ptr<nexus::system::SystemProvider> provider,
                 std::unique_ptr<HardwareRepository> repository,
                 nexus::notify::NotificationCenter& notifications, std::chrono::hours retention)
    : provider_(std::move(provider)), repository_(std::move(repository)),
      notifications_(&notifications), retention_(retention) {
}

Sampler::~Sampler() = default;

void Sampler::tick() {
    if (!active_.load(std::memory_order_relaxed) || provider_ == nullptr) {
        return;
    }
    const nexus::core::Timestamp now = nexus::core::now();

    std::vector<MetricSample> metrics;

    const auto cpu = provider_->cpu_load();
    metrics.push_back({"cpu.total", "", cpu.total});
    for (std::size_t i = 0; i < cpu.per_core.size(); ++i) {
        metrics.push_back({"cpu.core", std::to_string(i), cpu.per_core[i]});
    }

    const auto mem = provider_->memory_status();
    metrics.push_back({"mem.used_fraction", "", mem.used_fraction});
    metrics.push_back({"mem.used_bytes", "", static_cast<double>(mem.used_bytes)});
    metrics.push_back({"mem.total_bytes", "", static_cast<double>(mem.total_bytes)});

    for (const auto& disk : provider_->disks()) {
        metrics.push_back({"disk.free_bytes", disk.mount, static_cast<double>(disk.free_bytes)});
        metrics.push_back({"disk.total_bytes", disk.mount, static_cast<double>(disk.total_bytes)});
        if (disk.total_bytes > 0) {
            metrics.push_back(
                {"disk.free_fraction", disk.mount,
                 static_cast<double>(disk.free_bytes) / static_cast<double>(disk.total_bytes)});
        }
    }

    for (const auto& iface : provider_->network_interfaces()) {
        metrics.push_back({"net.up", iface.name, iface.up ? 1.0 : 0.0});
        metrics.push_back({"net.bytes_sent", iface.name, static_cast<double>(iface.bytes_sent)});
        metrics.push_back(
            {"net.bytes_received", iface.name, static_cast<double>(iface.bytes_received)});
        metrics.push_back(
            {"net.link_speed_bps", iface.name, static_cast<double>(iface.link_speed_bps)});
    }

    const auto battery = provider_->battery();
    metrics.push_back({"battery.present", "", battery.present ? 1.0 : 0.0});
    if (battery.present) {
        // charging/on_ac_power/charge_fraction are meaningless without a
        // battery - only recorded when one is actually present.
        metrics.push_back({"battery.charging", "", battery.charging ? 1.0 : 0.0});
        metrics.push_back({"battery.on_ac_power", "", battery.on_ac_power ? 1.0 : 0.0});
        metrics.push_back({"battery.charge_fraction", "", battery.charge_fraction});
    }

    repository_->record_metrics(metrics, now);
    evaluate_thresholds(metrics);

    auto processes = provider_->processes();
    std::sort(processes.begin(), processes.end(),
              [](const auto& a, const auto& b) { return a.cpu_fraction > b.cpu_fraction; });
    if (processes.size() > kProcessTopN) {
        processes.resize(kProcessTopN);
    }
    std::vector<ProcessSample> process_samples;
    process_samples.reserve(processes.size());
    for (const auto& proc : processes) {
        process_samples.push_back({proc.pid, proc.name, proc.cpu_fraction, proc.working_set_bytes});
    }
    repository_->record_processes(process_samples, now);

    if (++ticks_ % kPruneEveryTicks == 0) {
        repository_->prune_before(now - retention_);
    }
}

void Sampler::evaluate_thresholds(const std::vector<MetricSample>& samples) {
    std::unordered_map<std::string, double> readings;
    readings.reserve(samples.size());
    for (const MetricSample& sample : samples) {
        readings.emplace(key_of(sample.metric, sample.scope), sample.value);
    }

    for (const Threshold& threshold : repository_->thresholds(/*enabled_only=*/true)) {
        const auto reading = readings.find(key_of(threshold.metric, threshold.scope));
        if (reading == readings.end()) {
            continue;
        }
        const bool now_breached = threshold.breached_by(reading->second);
        const bool was_breached = breached_[threshold.id];
        if (now_breached == was_breached) {
            continue;
        }
        breached_[threshold.id] = now_breached;

        if (now_breached) {
            const auto severity = nexus::notify::severity_from_string(threshold.severity)
                                      .value_or(nexus::notify::Severity::Warning);
            notifications_->post("hardware", severity, "Threshold breached: " + threshold.metric,
                                 threshold.metric + " (" + threshold.scope +
                                     ") = " + std::to_string(reading->second));
        } else {
            notifications_->post("hardware", nexus::notify::Severity::Info,
                                 "Threshold recovered: " + threshold.metric, {});
        }
    }
}

} // namespace nexus::module::hardware
