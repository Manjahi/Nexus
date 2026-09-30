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
#include <icmpapi.h>
// clang-format on

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace nexus::net {

namespace {

using SteadyClock = std::chrono::steady_clock;

std::chrono::microseconds elapsed_since(SteadyClock::time_point start) {
    return std::chrono::duration_cast<std::chrono::microseconds>(SteadyClock::now() - start);
}

class WinsockScope {
public:
    WinsockScope() {
        WSADATA data{};
        ok_ = ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }
    ~WinsockScope() {
        if (ok_) {
            ::WSACleanup();
        }
    }
    WinsockScope(const WinsockScope&) = delete;
    WinsockScope& operator=(const WinsockScope&) = delete;

    [[nodiscard]] bool ok() const noexcept { return ok_; }

private:
    bool ok_ = false;
};

std::string address_text(const addrinfo* info) {
    std::array<char, INET6_ADDRSTRLEN> buffer{};
    const void* addr = nullptr;
    if (info->ai_family == AF_INET) {
        addr = &reinterpret_cast<const sockaddr_in*>(info->ai_addr)->sin_addr;
    } else if (info->ai_family == AF_INET6) {
        addr = &reinterpret_cast<const sockaddr_in6*>(info->ai_addr)->sin6_addr;
    }
    if (addr == nullptr) {
        return {};
    }
    if (::inet_ntop(info->ai_family, addr, buffer.data(), buffer.size()) == nullptr) {
        return {};
    }
    return std::string(buffer.data());
}

} // namespace

DnsResult resolve(std::string_view host) {
    DnsResult result;
    const WinsockScope winsock;
    if (!winsock.ok()) {
        result.status = ProbeStatus::Error;
        return result;
    }

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    const std::string host_str(host);
    addrinfo* results = nullptr;
    const auto start = SteadyClock::now();
    const int rc = ::getaddrinfo(host_str.c_str(), nullptr, &hints, &results);
    result.elapsed = elapsed_since(start);
    if (rc != 0) {
        result.status = ProbeStatus::DnsFailure;
        return result;
    }

    for (const addrinfo* p = results; p != nullptr; p = p->ai_next) {
        std::string text = address_text(p);
        if (!text.empty()) {
            result.addresses.push_back(std::move(text));
        }
    }
    ::freeaddrinfo(results);

    result.status = result.addresses.empty() ? ProbeStatus::DnsFailure : ProbeStatus::Ok;
    return result;
}

PingResult icmp_ping(std::string_view host, std::chrono::milliseconds timeout) {
    PingResult result;
    const WinsockScope winsock;
    if (!winsock.ok()) {
        result.status = ProbeStatus::Error;
        result.detail = "winsock init failed";
        return result;
    }

    addrinfo hints{};
    hints.ai_family = AF_INET; // ICMPv4 only
    const std::string host_str(host);
    addrinfo* results = nullptr;
    if (::getaddrinfo(host_str.c_str(), nullptr, &hints, &results) != 0 || results == nullptr) {
        result.status = ProbeStatus::DnsFailure;
        return result;
    }
    const IPAddr target = reinterpret_cast<const sockaddr_in*>(results->ai_addr)->sin_addr.s_addr;
    ::freeaddrinfo(results);

    const HANDLE icmp = ::IcmpCreateFile();
    if (icmp == INVALID_HANDLE_VALUE) {
        result.status = ProbeStatus::Error;
        result.detail = "IcmpCreateFile failed";
        return result;
    }

    std::array<char, 32> payload{};
    payload.fill('n');
    std::vector<char> reply(sizeof(ICMP_ECHO_REPLY) + payload.size() + 8);

    const auto start = SteadyClock::now();
    const DWORD count = ::IcmpSendEcho(
        icmp, target, payload.data(), static_cast<WORD>(payload.size()), nullptr, reply.data(),
        static_cast<DWORD>(reply.size()), static_cast<DWORD>(timeout.count()));
    const auto measured = elapsed_since(start);
    ::IcmpCloseHandle(icmp);

    if (count == 0) {
        const DWORD err = ::GetLastError();
        result.status = (err == IP_REQ_TIMED_OUT) ? ProbeStatus::Timeout : ProbeStatus::Unreachable;
        return result;
    }

    const auto* echo = reinterpret_cast<const ICMP_ECHO_REPLY*>(reply.data());
    if (echo->Status == IP_SUCCESS) {
        result.status = ProbeStatus::Ok;
        result.rtt = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::milliseconds{echo->RoundTripTime});
        if (result.rtt.count() == 0) {
            result.rtt = measured; // sub-millisecond loopback
        }
    } else if (echo->Status == IP_REQ_TIMED_OUT) {
        result.status = ProbeStatus::Timeout;
    } else {
        result.status = ProbeStatus::Unreachable;
    }
    return result;
}

TcpConnectResult tcp_connect(std::string_view host, std::uint16_t port,
                             std::chrono::milliseconds timeout) {
    TcpConnectResult result;
    const WinsockScope winsock;
    if (!winsock.ok()) {
        result.status = ProbeStatus::Error;
        result.detail = "winsock init failed";
        return result;
    }

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    const std::string host_str(host);
    const std::string port_str = std::to_string(port);
    addrinfo* results = nullptr;
    if (::getaddrinfo(host_str.c_str(), port_str.c_str(), &hints, &results) != 0) {
        result.status = ProbeStatus::DnsFailure;
        return result;
    }

    result.status = ProbeStatus::Unreachable;
    for (const addrinfo* p = results; p != nullptr; p = p->ai_next) {
        const SOCKET sock = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock == INVALID_SOCKET) {
            continue;
        }

        u_long non_blocking = 1;
        ::ioctlsocket(sock, FIONBIO, &non_blocking);

        const auto start = SteadyClock::now();
        const int rc = ::connect(sock, p->ai_addr, static_cast<int>(p->ai_addrlen));
        if (rc == 0) {
            result.status = ProbeStatus::Ok;
            result.elapsed = elapsed_since(start);
            ::closesocket(sock);
            break;
        }
        if (::WSAGetLastError() != WSAEWOULDBLOCK) {
            ::closesocket(sock);
            continue;
        }

        fd_set writable;
        FD_ZERO(&writable);
        FD_SET(sock, &writable);
        fd_set failed;
        FD_ZERO(&failed);
        FD_SET(sock, &failed);
        timeval tv{};
        tv.tv_sec = static_cast<long>(timeout.count() / 1000);
        tv.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);

        const int ready = ::select(0, nullptr, &writable, &failed, &tv);
        result.elapsed = elapsed_since(start);
        if (ready == 0) {
            result.status = ProbeStatus::Timeout;
            ::closesocket(sock);
            continue;
        }
        if (ready == SOCKET_ERROR || FD_ISSET(sock, &failed)) {
            result.status = ProbeStatus::Unreachable;
            ::closesocket(sock);
            continue;
        }

        int so_error = 0;
        int len = static_cast<int>(sizeof(so_error));
        ::getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&so_error), &len);
        ::closesocket(sock);
        if (so_error == 0) {
            result.status = ProbeStatus::Ok;
            break;
        }
        result.status = ProbeStatus::Unreachable;
    }
    ::freeaddrinfo(results);
    return result;
}

} // namespace nexus::net
