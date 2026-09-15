#include "nexus/module/network_center/device_monitor.hpp"

#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "nexus/notify/notification_center.hpp"
#include "nexus/notify/severity.hpp"

namespace nexus::module::network_center {

namespace {
constexpr int kPruneEveryTicks = 240;
}

DeviceMonitor::DeviceMonitor(std::unique_ptr<NetworkRepository> repository,
                             nexus::notify::NotificationCenter& notifications, PingFn ping,
                             std::chrono::hours retention)
    : repository_(std::move(repository)),
      notifications_(&notifications),
      ping_(std::move(ping)),
      retention_(retention) {}

DeviceMonitor::~DeviceMonitor() = default;

void DeviceMonitor::tick() {
    if (!active_.load(std::memory_order_relaxed) || !ping_) {
        return;
    }

    const std::vector<Device> devices = repository_->devices();
    if (devices.empty()) {
        return;
    }

    std::map<std::int64_t, std::vector<const Device*>> by_network;
    for (const Device& device : devices) {
        by_network[device.network_id].push_back(&device);
    }

    for (const auto& [network_id, group] : by_network) {
        const auto now = nexus::core::now();
        const std::int64_t check_id = repository_->begin_check(network_id, "monitor", now);
        int online_count = 0;

        for (const Device* device : group) {
            const PingReading reading = ping_(*device);
            const std::string_view status = reading.ok ? "online" : "offline";
            const auto at = nexus::core::now();

            repository_->record_check_result(check_id, device->id, device->address, status,
                                             reading.rtt, at);
            repository_->set_device_status(device->id, status, at);
            if (reading.ok) {
                ++online_count;
            }

            const std::string label = device->label.empty()
                                          ? (device->hostname.empty() ? device->address : device->hostname)
                                          : device->label;
            const auto previous = last_status_.find(device->id);
            if (previous != last_status_.end() && previous->second != status) {
                if (status == "offline") {
                    notifications_->post("network_center", nexus::notify::Severity::Warning,
                                         "Device offline: " + label, device->address);
                } else {
                    notifications_->post("network_center", nexus::notify::Severity::Info,
                                         "Device back online: " + label, device->address);
                }
            }
            last_status_[device->id] = std::string(status);
        }

        repository_->finish_check(check_id, online_count, now);
    }

    if (++ticks_ % kPruneEveryTicks == 0) {
        repository_->prune_before(nexus::core::now() - retention_);
    }
}

} // namespace nexus::module::network_center
