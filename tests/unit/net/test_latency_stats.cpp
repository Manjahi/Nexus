#include "nexus/net/latency_stats.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <vector>

using namespace std::chrono_literals;
using nexus::net::PingResult;
using nexus::net::ProbeStatus;
using nexus::net::summarize;

namespace {

PingResult ok(std::chrono::microseconds rtt) {
    return PingResult{ProbeStatus::Ok, rtt, {}};
}

PingResult lost() {
    return PingResult{ProbeStatus::Timeout, {}, {}};
}

} // namespace

TEST_CASE("empty sample set yields zeroes", "[net][stats]") {
    const auto s = summarize({});
    REQUIRE(s.sent == 0);
    REQUIRE(s.received == 0);
    REQUIRE(s.loss_fraction == 0.0);
    REQUIRE(s.mean == 0us);
    REQUIRE(s.jitter == 0us);
}

TEST_CASE("all responses: min/max/mean/jitter", "[net][stats]") {
    const std::vector<PingResult> samples{ok(10ms), ok(20ms), ok(30ms)};
    const auto s = summarize(samples);

    REQUIRE(s.sent == 3);
    REQUIRE(s.received == 3);
    REQUIRE(s.loss_fraction == 0.0);
    REQUIRE(s.min == 10ms);
    REQUIRE(s.max == 30ms);
    REQUIRE(s.mean == 20ms);
    REQUIRE(s.jitter == 10ms); // mean(|20-10|, |30-20|)
}

TEST_CASE("partial loss counts toward loss only", "[net][stats]") {
    const std::vector<PingResult> samples{ok(10ms), lost(), ok(30ms)};
    const auto s = summarize(samples);

    REQUIRE(s.sent == 3);
    REQUIRE(s.received == 2);
    REQUIRE(s.loss_fraction == Catch::Approx(1.0 / 3.0));
    REQUIRE(s.min == 10ms);
    REQUIRE(s.max == 30ms);
    REQUIRE(s.mean == 20ms);
    REQUIRE(s.jitter == 20ms); // only one consecutive pair of received samples
}

TEST_CASE("total loss leaves timing stats zero", "[net][stats]") {
    const auto s = summarize({lost(), lost()});
    REQUIRE(s.sent == 2);
    REQUIRE(s.received == 0);
    REQUIRE(s.loss_fraction == 1.0);
    REQUIRE(s.min == 0us);
    REQUIRE(s.max == 0us);
}
