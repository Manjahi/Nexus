#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

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
    /// `ping` defaults to a real ICMP probe (default_ping); tests inject a
    /// fake to avoid depending on real network I/O, the same shape as
    /// DeviceMonitor's injectable PingFn.
    explicit NetworkScanner(NetworkRepository& repository,
                            ScanPingFn ping = &NetworkScanner::default_ping) noexcept
        : repository_(&repository), ping_(std::move(ping)) {}

    /// `network_id` must already exist (see NetworkRepository::add_network) and
    /// `cidr` should be that network's stored range.
    [[nodiscard]] ScanSummary scan(std::int64_t network_id, std::string_view cidr,
                                   const ScanProgressFn& on_progress = {},
                                   const ScanCancelFn& should_cancel = {});

    /// The real ICMP probe used outside tests.
    [[nodiscard]] static nexus::net::PingResult default_ping(std::string_view address);

private:
    NetworkRepository* repository_;
    ScanPingFn ping_;
};

} // namespace nexus::module::network_center
