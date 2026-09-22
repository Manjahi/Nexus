#include "nexus/net/probe.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>

#include <iphlpapi.h>

namespace nexus::net {

std::optional<std::string> default_gateway() {
    // GetBestRoute's "destination" doesn't need to be reachable - it's only
    // used to pick which of the OS's routes would carry traffic there, so
    // any public IP works as a stand-in for "the route to the internet."
    // 8.8.8.8 is already used elsewhere in this app as a default ICMP probe
    // target (see connectivity's seeded probe_targets).
    in_addr dest{};
    if (::inet_pton(AF_INET, "8.8.8.8", &dest) != 1) {
        return std::nullopt;
    }

    MIB_IPFORWARDROW row{};
    if (::GetBestRoute(dest.S_un.S_addr, 0, &row) != NO_ERROR) {
        return std::nullopt;
    }
    if (row.dwForwardNextHop == 0) {
        return std::nullopt; // e.g. a directly-connected route with no gateway
    }

    in_addr next_hop{};
    next_hop.S_un.S_addr = row.dwForwardNextHop;
    char buf[INET_ADDRSTRLEN] = {};
    if (::inet_ntop(AF_INET, &next_hop, buf, sizeof(buf)) == nullptr) {
        return std::nullopt;
    }
    return std::string(buf);
}

} // namespace nexus::net
