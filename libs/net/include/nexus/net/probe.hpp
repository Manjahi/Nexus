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

struct HttpProbeResult {
    ProbeStatus status = ProbeStatus::Error;
    int status_code = 0;   ///< HTTP response code; 0 if no response was received
    std::chrono::microseconds elapsed{0};
    std::uint64_t bytes_received = 0;
    std::string detail;

    /// True when an HTTP response was received (of any status code).
    [[nodiscard]] bool ok() const noexcept { return status == ProbeStatus::Ok; }
    [[nodiscard]] bool healthy() const noexcept {
        return ok() && status_code >= 200 && status_code < 400;
    }
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

/// Issues an HTTP(S) GET and reports the response code and timing. Redirects are
/// followed; the body is discarded but its size is counted.
[[nodiscard]] HttpProbeResult http_probe(std::string_view url,
                                         std::chrono::milliseconds timeout =
                                             std::chrono::milliseconds{5000});

} // namespace nexus::net
