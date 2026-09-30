#include "nexus/module/network_center/cidr.hpp"

#include <algorithm>
#include <array>
#include <charconv>

namespace nexus::module::network_center {

std::optional<std::uint32_t> parse_ipv4(std::string_view text) {
    std::array<int, 4> octets{};
    std::size_t start = 0;
    for (int i = 0; i < 4; ++i) {
        const std::size_t dot = (i < 3) ? text.find('.', start) : text.size();
        if (dot == std::string_view::npos || dot == start) {
            return std::nullopt;
        }
        const std::string_view part = text.substr(start, dot - start);
        if (part.size() > 3) {
            return std::nullopt;
        }
        int value = 0;
        const auto [ptr, ec] = std::from_chars(part.data(), part.data() + part.size(), value);
        if (ec != std::errc{} || ptr != part.data() + part.size() || value < 0 || value > 255) {
            return std::nullopt;
        }
        octets[static_cast<std::size_t>(i)] = value;
        start = dot + 1;
    }
    return (static_cast<std::uint32_t>(octets[0]) << 24) |
           (static_cast<std::uint32_t>(octets[1]) << 16) |
           (static_cast<std::uint32_t>(octets[2]) << 8) | static_cast<std::uint32_t>(octets[3]);
}

std::string format_ipv4(std::uint32_t address) {
    return std::to_string((address >> 24) & 0xFF) + "." + std::to_string((address >> 16) & 0xFF) +
           "." + std::to_string((address >> 8) & 0xFF) + "." + std::to_string(address & 0xFF);
}

std::optional<CidrRange> parse_cidr(std::string_view text) {
    const std::size_t slash = text.find('/');
    if (slash == std::string_view::npos) {
        return std::nullopt;
    }
    const auto address = parse_ipv4(text.substr(0, slash));
    if (!address) {
        return std::nullopt;
    }
    const std::string_view prefix_text = text.substr(slash + 1);
    int prefix = -1;
    const auto [ptr, ec] =
        std::from_chars(prefix_text.data(), prefix_text.data() + prefix_text.size(), prefix);
    if (ec != std::errc{} || ptr != prefix_text.data() + prefix_text.size() || prefix < 0 ||
        prefix > 32) {
        return std::nullopt;
    }
    return CidrRange{*address, static_cast<std::uint32_t>(prefix)};
}

std::vector<std::string> host_addresses(const CidrRange& range, std::size_t max_hosts) {
    std::vector<std::string> out;
    if (range.prefix_len >= 31) {
        // /31 and /32: too small to have distinct network/broadcast addresses;
        // treat every address in range as a host.
        const std::uint32_t count = range.prefix_len == 31 ? 2 : 1;
        const std::uint32_t mask =
            range.prefix_len == 0 ? 0 : (0xFFFFFFFFu << (32 - range.prefix_len));
        const std::uint32_t base = range.address & mask;
        for (std::uint32_t i = 0; i < count; ++i) {
            out.push_back(format_ipv4(base + i));
        }
        return out;
    }

    const std::uint32_t mask = 0xFFFFFFFFu << (32 - range.prefix_len);
    const std::uint32_t network = range.address & mask;
    const std::uint32_t broadcast = network | ~mask;

    out.reserve(std::min<std::size_t>(max_hosts, broadcast - network));
    for (std::uint32_t addr = network + 1; addr < broadcast && out.size() < max_hosts; ++addr) {
        out.push_back(format_ipv4(addr));
    }
    return out;
}

} // namespace nexus::module::network_center
