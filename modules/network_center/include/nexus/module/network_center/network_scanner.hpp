#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace nexus::module::network_center {

class NetworkRepository;

struct ScanProgress {
    std::size_t scanned = 0;
    std::size_t total = 0;
};

using ScanProgressFn = std::function<void(ScanProgress)>;
using ScanCancelFn = std::function<bool()>;

struct ScanSummary {
    std::int64_t network_id = 0;
    int hosts_probed = 0;
    int devices_found = 0;
    bool cancelled = false;
};

/// Discovers live hosts on a user-authorized CIDR range by ICMP-pinging every
/// candidate address (see cidr.hpp for the consent-gate rationale: NexusPC
/// never enumerates a range the user did not type in themselves). Responding
/// hosts are upserted into `devices` and the run is logged as a `checks` row.
class NetworkScanner {
public:
    explicit NetworkScanner(NetworkRepository& repository) noexcept : repository_(&repository) {}

    /// `network_id` must already exist (see NetworkRepository::add_network) and
    /// `cidr` should be that network's stored range.
    [[nodiscard]] ScanSummary scan(std::int64_t network_id, std::string_view cidr,
                                   const ScanProgressFn& on_progress = {},
                                   const ScanCancelFn& should_cancel = {});

private:
    NetworkRepository* repository_;
};

} // namespace nexus::module::network_center
