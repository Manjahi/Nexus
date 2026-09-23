#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
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

/// The default gateway's IP (the next hop on the OS's best route to the
/// public internet, e.g. the router), or nullopt if it can't be determined
/// (no default route - offline, or a VPN/tunnel config with none).
[[nodiscard]] std::optional<std::string> default_gateway();

/// Resolves `ipv4_address`'s MAC address via ARP (Windows: SendARP), e.g.
/// "AA:BB:CC:DD:EE:FF". No raw sockets or elevated privileges needed.
/// nullopt if the address doesn't answer ARP (offline, or on a different
/// subnet SendARP can't reach directly) or isn't a valid IPv4 address.
[[nodiscard]] std::optional<std::string> arp_resolve(std::string_view ipv4_address);

} // namespace nexus::net
