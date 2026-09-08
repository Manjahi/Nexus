#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace nexus::net {

enum class ProbeStatus {
    Ok,
    Timeout,
    Unreachable,
    DnsFailure,
    Error,
};

[[nodiscard]] std::string_view to_string(ProbeStatus status) noexcept;

struct DnsResult {
    ProbeStatus status = ProbeStatus::Error;
    std::vector<std::string> addresses;
    std::chrono::microseconds elapsed{0};

    [[nodiscard]] bool ok() const noexcept { return status == ProbeStatus::Ok; }
};

struct PingResult {
    ProbeStatus status = ProbeStatus::Error;
    std::chrono::microseconds rtt{0};
    std::string detail;

    [[nodiscard]] bool ok() const noexcept { return status == ProbeStatus::Ok; }
};

struct TcpConnectResult {
    ProbeStatus status = ProbeStatus::Error;
    std::chrono::microseconds elapsed{0};
    std::string detail;

    [[nodiscard]] bool ok() const noexcept { return status == ProbeStatus::Ok; }
};

/// Resolves `host` to one or more textual IP addresses (A and AAAA).
[[nodiscard]] DnsResult resolve(std::string_view host);

/// Sends a single ICMPv4 echo request. `host` may be a name or a dotted address.
[[nodiscard]] PingResult icmp_ping(std::string_view host,
                                   std::chrono::milliseconds timeout = std::chrono::milliseconds{1000});

/// Attempts a TCP connection and reports how long establishing it took.
[[nodiscard]] TcpConnectResult tcp_connect(std::string_view host, std::uint16_t port,
                                           std::chrono::milliseconds timeout =
                                               std::chrono::milliseconds{2000});

} // namespace nexus::net
