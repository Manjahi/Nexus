#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "nexus/net/probe.hpp"

namespace nexus::module::network_center {

class NetworkRepository;

struct ScanProgress {
    std::size_t scanned = 0;
    std::size_t total = 0;
};

using ScanProgressFn = std::function<void(ScanProgress)>;
using ScanCancelFn = std::function<bool()>;
using ScanPingFn = std::function<nexus::net::PingResult(std::string_view address)>;
/// Returns the subset of a fixed, common-service port list (see
/// default_port_check) found open on `address`.
using ScanPortCheckFn = std::function<std::vector<std::uint16_t>(std::string_view address)>;
/// Returns `address`'s MAC ("AA:BB:CC:DD:EE:FF"), or nullopt if ARP
/// resolution failed. Only called for hosts that already answered a ping
/// (see scan()) - SendARP's own retry/timeout for a genuinely absent host
/// can take multiple seconds, and most CIDR ranges have far more absent
/// than present hosts, so ARPing every candidate address regardless of
/// ping result would turn a several-second sweep into a multi-minute one.
/// This trades away resolving ICMP-blocking-but-present devices (an
/// explicitly named benefit of ARP in the gap-closure plan) for keeping
/// scan latency close to what it was before ARP existed.
using ScanArpFn = std::function<std::optional<std::string>(std::string_view address)>;

struct ScanSummary {
    std::int64_t network_id = 0;
    int hosts_probed = 0;
    int devices_found = 0;
    bool cancelled = false;
};

/// Discovers live hosts on a user-authorized CIDR range by pinging every
/// candidate address (see cidr.hpp for the consent-gate rationale: NexusPC
/// never enumerates a range the user did not type in themselves). Responding
/// hosts are upserted into `devices` and the run is logged as a `checks` row.
class NetworkScanner {
public:
    /// `ping`/`port_check`/`arp` default to real ICMP/TCP/ARP probes; tests
    /// inject fakes to avoid depending on real network I/O, the same shape
    /// as DeviceMonitor's injectable PingFn.
    explicit NetworkScanner(NetworkRepository& repository,
                            ScanPingFn ping = &NetworkScanner::default_ping,
                            ScanPortCheckFn port_check = &NetworkScanner::default_port_check,
                            ScanArpFn arp = &NetworkScanner::default_arp_resolve) noexcept
        : repository_(&repository),
          ping_(std::move(ping)),
          port_check_(std::move(port_check)),
          arp_(std::move(arp)) {}

    /// `network_id` must already exist (see NetworkRepository::add_network) and
    /// `cidr` should be that network's stored range.
    [[nodiscard]] ScanSummary scan(std::int64_t network_id, std::string_view cidr,
                                   const ScanProgressFn& on_progress = {},
                                   const ScanCancelFn& should_cancel = {});

    /// The real ICMP probe used outside tests.
    [[nodiscard]] static nexus::net::PingResult default_ping(std::string_view address);

    /// The real TCP port-check used outside tests: tries a small fixed list
    /// of common service ports (80 http, 443 https, 22 ssh, 445 smb, 3389
    /// rdp) with a short per-port timeout, returns the ones that connected.
    [[nodiscard]] static std::vector<std::uint16_t> default_port_check(std::string_view address);

    /// The real ARP resolution used outside tests (nexus::net::arp_resolve).
    [[nodiscard]] static std::optional<std::string> default_arp_resolve(std::string_view address);

private:
    NetworkRepository* repository_;
    ScanPingFn ping_;
    ScanPortCheckFn port_check_;
    ScanArpFn arp_;
};

} // namespace nexus::module::network_center
