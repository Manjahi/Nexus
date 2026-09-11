#include "nexus/module/network_center/network_scanner.hpp"

#include <chrono>

#include "nexus/core/time.hpp"
#include "nexus/module/network_center/cidr.hpp"
#include "nexus/module/network_center/network_repository.hpp"
#include "nexus/net/probe.hpp"

namespace nexus::module::network_center {

namespace {
constexpr std::chrono::milliseconds kPingTimeout{300};
}

ScanSummary NetworkScanner::scan(std::int64_t network_id, std::string_view cidr,
                                 const ScanProgressFn& on_progress,
                                 const ScanCancelFn& should_cancel) {
    ScanSummary summary;
    summary.network_id = network_id;

    const auto range = parse_cidr(cidr);
    if (!range) {
        return summary;
    }
    const std::vector<std::string> hosts = host_addresses(*range);

    const auto now = nexus::core::now();
    const std::int64_t check_id = repository_->begin_check(network_id, "discovery", now);

    for (std::size_t i = 0; i < hosts.size(); ++i) {
        if (should_cancel && should_cancel()) {
            summary.cancelled = true;
            break;
        }

        const std::string& address = hosts[i];
        const nexus::net::PingResult ping = nexus::net::icmp_ping(address, kPingTimeout);
        ++summary.hosts_probed;

        if (ping.ok()) {
            const std::int64_t device_id =
                repository_->upsert_device(network_id, address, "", nexus::core::now());
            repository_->record_check_result(check_id, device_id, address, "online", ping.rtt,
                                             nexus::core::now());
            ++summary.devices_found;
        }

        if (on_progress) {
            on_progress(ScanProgress{i + 1, hosts.size()});
        }
    }

    repository_->finish_check(check_id, summary.devices_found, nexus::core::now());
    return summary;
}

} // namespace nexus::module::network_center
