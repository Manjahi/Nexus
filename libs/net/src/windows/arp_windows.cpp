#include "nexus/net/probe.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

// clang-format off
#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>

#include <iphlpapi.h>
// clang-format on

#include <cstdio>
#include <string>

namespace nexus::net {

std::optional<std::string> arp_resolve(std::string_view ipv4_address) {
    in_addr addr{};
    // inet_pton needs a null-terminated string; ipv4_address is always a
    // short dotted-quad, so a std::string copy is cheap and simplest.
    if (::inet_pton(AF_INET, std::string(ipv4_address).c_str(), &addr) != 1) {
        return std::nullopt;
    }

    // SendARP writes up to 8 bytes (a physical address can be up to
    // MAXLEN_PHYSADDR = 8), so a 2-element ULONG array is exactly that,
    // DWORD-aligned as the API expects.
    ULONG mac[2] = {0, 0};
    ULONG mac_len = sizeof(mac);
    const DWORD result = ::SendARP(addr.S_un.S_addr, 0, mac, &mac_len);
    if (result != NO_ERROR || mac_len < 6) {
        return std::nullopt;
    }

    const auto* bytes = reinterpret_cast<const unsigned char*>(mac);
    char buf[18] = {};
    std::snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", bytes[0], bytes[1], bytes[2],
                  bytes[3], bytes[4], bytes[5]);
    return std::string(buf);
}

} // namespace nexus::net
