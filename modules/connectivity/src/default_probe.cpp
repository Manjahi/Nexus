#include "nexus/module/connectivity/prober.hpp"
#include "nexus/net/probe.hpp"

#include <chrono>

namespace nexus::module::connectivity {

namespace {

using namespace std::chrono_literals;

std::string status_of(nexus::net::ProbeStatus status) {
    return std::string(nexus::net::to_string(status));
}

} // namespace

ProbeReading Prober::default_probe(const ProbeTarget& target) {
    ProbeReading reading;

    switch (target.kind) {
        case ProbeKind::Icmp: {
            const auto result = nexus::net::icmp_ping(target.address, 1000ms);
            reading.status = status_of(result.status);
            if (result.ok()) {
                reading.rtt = result.rtt;
            }
            reading.detail = result.detail;
            break;
        }
        case ProbeKind::Tcp: {
            const std::uint16_t port = target.port.value_or(80);
            const auto result = nexus::net::tcp_connect(target.address, port, 1500ms);
            reading.status = status_of(result.status);
            if (result.ok()) {
                reading.rtt = result.elapsed;
            }
            reading.detail = result.detail;
            break;
        }
        case ProbeKind::Http: {
            const auto result = nexus::net::http_probe(target.address, 3000ms);
            if (result.ok()) {
                reading.status = result.healthy() ? "ok" : "error";
                reading.rtt = result.elapsed;
                reading.detail = "HTTP " + std::to_string(result.status_code);
            } else {
                reading.status = status_of(result.status);
                reading.detail = result.detail;
            }
            break;
        }
        case ProbeKind::Dns: {
            const auto result = nexus::net::resolve(target.address);
            reading.status = status_of(result.status);
            if (result.ok()) {
                reading.rtt = result.elapsed;
                reading.detail =
                    result.addresses.empty() ? "" : "resolved to " + result.addresses.front();
            }
            break;
        }
    }

    return reading;
}

} // namespace nexus::module::connectivity
