#include "nexus/net/probe.hpp"

namespace nexus::net {

std::string_view to_string(ProbeStatus status) noexcept {
    switch (status) {
        case ProbeStatus::Ok:
            return "ok";
        case ProbeStatus::Timeout:
            return "timeout";
        case ProbeStatus::Unreachable:
            return "unreachable";
        case ProbeStatus::DnsFailure:
            return "dns_failure";
        case ProbeStatus::Error:
            return "error";
    }
    return "error";
}

} // namespace nexus::net
