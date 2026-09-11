#include "nexus/module/network_center/cidr.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace nexus::module::network_center;

TEST_CASE("parse_ipv4 accepts well-formed dotted quads", "[network_center][cidr]") {
    const auto addr = parse_ipv4("192.168.1.10");
    REQUIRE(addr.has_value());
    REQUIRE(format_ipv4(*addr) == "192.168.1.10");

    REQUIRE(parse_ipv4("0.0.0.0").has_value());
    REQUIRE(parse_ipv4("255.255.255.255").has_value());
}

TEST_CASE("parse_ipv4 rejects malformed input", "[network_center][cidr]") {
    REQUIRE_FALSE(parse_ipv4("256.1.1.1").has_value());
    REQUIRE_FALSE(parse_ipv4("1.1.1").has_value());
    REQUIRE_FALSE(parse_ipv4("1.1.1.1.1").has_value());
    REQUIRE_FALSE(parse_ipv4("1.1.1.").has_value());
    REQUIRE_FALSE(parse_ipv4("a.b.c.d").has_value());
    REQUIRE_FALSE(parse_ipv4("").has_value());
}

TEST_CASE("parse_cidr splits address and prefix", "[network_center][cidr]") {
    const auto range = parse_cidr("192.168.1.0/24");
    REQUIRE(range.has_value());
    REQUIRE(range->prefix_len == 24);
    REQUIRE(format_ipv4(range->address) == "192.168.1.0");

    REQUIRE_FALSE(parse_cidr("192.168.1.0").has_value());
    REQUIRE_FALSE(parse_cidr("192.168.1.0/33").has_value());
    REQUIRE_FALSE(parse_cidr("192.168.1.0/-1").has_value());
    REQUIRE_FALSE(parse_cidr("bad/24").has_value());
}

TEST_CASE("host_addresses excludes network and broadcast for a /24", "[network_center][cidr]") {
    const auto range = parse_cidr("10.0.0.0/24");
    REQUIRE(range.has_value());

    const auto hosts = host_addresses(*range);
    REQUIRE(hosts.size() == 254);
    REQUIRE(hosts.front() == "10.0.0.1");
    REQUIRE(hosts.back() == "10.0.0.254");
}

TEST_CASE("host_addresses handles /31 and /32 specially", "[network_center][cidr]") {
    const auto p31 = parse_cidr("10.0.0.0/31");
    REQUIRE(p31.has_value());
    const auto hosts31 = host_addresses(*p31);
    REQUIRE(hosts31.size() == 2);
    REQUIRE(hosts31[0] == "10.0.0.0");
    REQUIRE(hosts31[1] == "10.0.0.1");

    const auto p32 = parse_cidr("10.0.0.5/32");
    REQUIRE(p32.has_value());
    const auto hosts32 = host_addresses(*p32);
    REQUIRE(hosts32.size() == 1);
    REQUIRE(hosts32[0] == "10.0.0.5");
}

TEST_CASE("host_addresses respects the max_hosts cap", "[network_center][cidr]") {
    const auto range = parse_cidr("10.0.0.0/16");
    REQUIRE(range.has_value());

    const auto hosts = host_addresses(*range, /*max_hosts=*/10);
    REQUIRE(hosts.size() == 10);
}
