#include "nexus/module/network_center/network_scanner.hpp"

#include <array>
#include <chrono>
#include <string>

#include "nexus/core/time.hpp"
#include "nexus/module/network_center/cidr.hpp"
#include "nexus/module/network_center/network_repository.hpp"
#include "nexus/net/probe.hpp"

namespace nexus::module::network_center {

namespace {
constexpr std::chrono::milliseconds kPingTimeout{300};
constexpr std::chrono::milliseconds kPortCheckTimeout{400};
// http, https, ssh, smb, rdp - the small set of common services this app
// checks for without the user naming a specific service to probe.
constexpr std::array<std::uint16_t, 5> kCommonPorts{80, 443, 22, 445, 3389};

std::string join_ports(const std::vector<std::uint16_t>& ports) {
    std::string out;
    for (std::size_t i = 0; i < ports.size(); ++i) {
        if (i > 0) {
            out += ',';
        }
        out += std::to_string(ports[i]);
    }
    return out;
}

} // namespace

nexus::net::PingResult NetworkScanner::default_ping(std::string_view address) {
    return nexus::net::icmp_ping(address, kPingTimeout);
}

std::vector<std::uint16_t> NetworkScanner::default_port_check(std::string_view address) {
    std::vector<std::uint16_t> open;
    for (const std::uint16_t port : kCommonPorts) {
        if (nexus::net::tcp_connect(address, port, kPortCheckTimeout).ok()) {
            open.push_back(port);
        }
    }
    return open;
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
        const nexus::net::PingResult ping = ping_(address);
        ++summary.hosts_probed;

        if (ping.ok()) {
            const std::int64_t device_id =
                repository_->upsert_device(network_id, address, "", nexus::core::now());
            repository_->record_check_result(check_id, device_id, address, "online", ping.rtt,
                                             nexus::core::now());
            ++summary.devices_found;

            if (port_check_) {
                repository_->set_device_open_ports(device_id, join_ports(port_check_(address)));
            }
        }

        if (on_progress) {
            on_progress(ScanProgress{i + 1, hosts.size()});
        }
    }

    repository_->finish_check(check_id, summary.devices_found, nexus::core::now());
    return summary;
}

} // namespace nexus::module::network_center
