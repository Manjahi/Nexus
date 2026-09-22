#include "nexus/net/probe.hpp"

// Placeholder for platforms without a probe backend yet.

namespace nexus::net {

DnsResult resolve(std::string_view) {
    return {};
}

PingResult icmp_ping(std::string_view, std::chrono::milliseconds) {
    return {};
}

TcpConnectResult tcp_connect(std::string_view, std::uint16_t, std::chrono::milliseconds) {
    return {};
}

std::optional<std::string> default_gateway() {
    return std::nullopt;
}

} // namespace nexus::net
