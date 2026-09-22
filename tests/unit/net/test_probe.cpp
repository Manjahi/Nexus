#include "nexus/net/probe.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

namespace {

// A loopback TCP listener on an ephemeral port, so tcp_connect has a guaranteed
// target.
class LoopbackListener {
public:
    LoopbackListener() {
        WSADATA data{};
        WSAStartup(MAKEWORD(2, 2), &data);
        sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        bind(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        listen(sock_, 4);
        int len = sizeof(addr);
        getsockname(sock_, reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);
    }
    ~LoopbackListener() {
        if (sock_ != INVALID_SOCKET) {
            closesocket(sock_);
        }
        WSACleanup();
    }
    LoopbackListener(const LoopbackListener&) = delete;
    LoopbackListener& operator=(const LoopbackListener&) = delete;

    [[nodiscard]] std::uint16_t port() const { return port_; }

private:
    SOCKET sock_ = INVALID_SOCKET;
    std::uint16_t port_ = 0;
};

} // namespace
#endif

using namespace std::chrono_literals;
using nexus::net::ProbeStatus;

TEST_CASE("resolve maps localhost to a loopback address", "[net][dns]") {
    const auto result = nexus::net::resolve("localhost");
    REQUIRE(result.ok());
    REQUIRE_FALSE(result.addresses.empty());
    const bool has_loopback =
        std::any_of(result.addresses.begin(), result.addresses.end(), [](const std::string& a) {
            return a == "127.0.0.1" || a == "::1";
        });
    REQUIRE(has_loopback);
}

TEST_CASE("resolve fails for a bogus name", "[net][dns]") {
    const auto result = nexus::net::resolve("nx-does-not-exist.invalid");
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.status == ProbeStatus::DnsFailure);
}

#ifdef _WIN32
TEST_CASE("tcp_connect reaches a live loopback listener", "[net][tcp]") {
    LoopbackListener listener;
    const auto result = nexus::net::tcp_connect("127.0.0.1", listener.port(), 1s);
    REQUIRE(result.ok());
    REQUIRE(result.elapsed.count() >= 0);
}

TEST_CASE("tcp_connect to an unrouted address does not succeed", "[net][tcp]") {
    // 192.0.2.0/24 is TEST-NET-1 (RFC 5737): guaranteed not routable.
    const auto result = nexus::net::tcp_connect("192.0.2.1", 80, 400ms);
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("icmp_ping reaches loopback", "[net][icmp]") {
    const auto result = nexus::net::icmp_ping("127.0.0.1", 1s);
    REQUIRE(result.ok());
    REQUIRE(result.rtt.count() >= 0);
}

TEST_CASE("http_probe against a live loopback listener that never replies times out",
          "[net][http]") {
    LoopbackListener listener;
    const auto url = "http://127.0.0.1:" + std::to_string(listener.port()) + "/";
    const auto result = nexus::net::http_probe(url, 400ms);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.status != ProbeStatus::DnsFailure);
}
#endif

TEST_CASE("http_probe rejects a malformed URL", "[net][http]") {
    const auto result = nexus::net::http_probe("not a url", 500ms);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.status == ProbeStatus::Error);
}

TEST_CASE("http_probe to an unrouted address does not succeed", "[net][http]") {
    const auto result = nexus::net::http_probe("http://192.0.2.1/", 400ms);
    REQUIRE_FALSE(result.ok());
    REQUIRE((result.status == ProbeStatus::Timeout || result.status == ProbeStatus::Unreachable));
}

#ifdef _WIN32
TEST_CASE("default_gateway returns a well-formed IPv4 address when one exists",
         "[net][gateway]") {
    // This machine's actual gateway isn't knowable in advance (varies by
    // network/CI runner), so this only checks structural validity - nullopt
    // (no default route: offline, or a VPN config with none) is also a
    // legitimate outcome and not asserted against.
    const auto gateway = nexus::net::default_gateway();
    if (!gateway) {
        return;
    }
    in_addr addr{};
    REQUIRE(::inet_pton(AF_INET, gateway->c_str(), &addr) == 1);
    // The one thing that's always true of a *default* route's next hop:
    // it's never the unspecified address.
    REQUIRE(addr.s_addr != 0);
}
#endif
