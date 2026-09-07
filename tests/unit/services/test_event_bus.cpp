#include "nexus/services/event_bus.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using nexus::services::EventBus;

namespace {
struct Ping {
    int value = 0;
};
struct Pong {
    std::string text;
};
} // namespace

TEST_CASE("handlers receive only their event type", "[services][eventbus]") {
    EventBus bus;
    std::vector<int> pings;
    std::vector<std::string> pongs;

    bus.subscribe<Ping>([&](const Ping& p) { pings.push_back(p.value); });
    bus.subscribe<Pong>([&](const Pong& p) { pongs.push_back(p.text); });

    bus.publish(Ping{1});
    bus.publish(Pong{"a"});
    bus.publish(Ping{2});

    REQUIRE(pings == std::vector<int>{1, 2});
    REQUIRE(pongs == std::vector<std::string>{"a"});
}

TEST_CASE("multiple handlers fire in subscription order", "[services][eventbus]") {
    EventBus bus;
    std::vector<int> order;
    bus.subscribe<Ping>([&](const Ping&) { order.push_back(1); });
    bus.subscribe<Ping>([&](const Ping&) { order.push_back(2); });
    bus.subscribe<Ping>([&](const Ping&) { order.push_back(3); });

    bus.publish(Ping{});
    REQUIRE(order == std::vector<int>{1, 2, 3});
}

TEST_CASE("unsubscribe stops delivery", "[services][eventbus]") {
    EventBus bus;
    int hits = 0;
    const auto token = bus.subscribe<Ping>([&](const Ping&) { ++hits; });

    bus.publish(Ping{});
    REQUIRE(bus.unsubscribe(token));
    bus.publish(Ping{});

    REQUIRE(hits == 1);
    REQUIRE_FALSE(bus.unsubscribe(token));
    REQUIRE(bus.handler_count() == 0);
}

TEST_CASE("publishing with no subscribers is a no-op", "[services][eventbus]") {
    EventBus bus;
    REQUIRE_NOTHROW(bus.publish(Ping{42}));
}
