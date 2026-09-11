#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nexus::module::network_center {

struct CidrRange {
    std::uint32_t address = 0;    ///< any address in the range, host byte order
    std::uint32_t prefix_len = 0; ///< 0-32
};

[[nodiscard]] std::optional<std::uint32_t> parse_ipv4(std::string_view text);
[[nodiscard]] std::string format_ipv4(std::uint32_t address);

[[nodiscard]] std::optional<CidrRange> parse_cidr(std::string_view text);

/// Every usable host address in the range (network and broadcast excluded for
/// prefixes <= 30), dotted-quad, ascending. Capped at `max_hosts` to keep a
/// mistyped huge range from enumerating millions of addresses; the largest
/// range NexusPC's positioning calls for (a lab/office subnet) is well under
/// this.
[[nodiscard]] std::vector<std::string> host_addresses(const CidrRange& range,
                                                      std::size_t max_hosts = 1024);

} // namespace nexus::module::network_center
