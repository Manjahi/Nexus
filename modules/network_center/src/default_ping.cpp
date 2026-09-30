#include "nexus/module/network_center/device_monitor.hpp"
#include "nexus/net/probe.hpp"

#include <chrono>

namespace nexus::module::network_center {

namespace {
using namespace std::chrono_literals;
}

PingReading DeviceMonitor::default_ping(const Device& device) {
    PingReading reading;
    const auto result = nexus::net::icmp_ping(device.address, 500ms);
    reading.ok = result.ok();
    if (result.ok()) {
        reading.rtt = result.rtt;
    }
    return reading;
}

} // namespace nexus::module::network_center
