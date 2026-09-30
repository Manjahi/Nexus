#include "nexus/notify/notification_center.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

using nexus::notify::Notification;
using nexus::notify::NotificationCenter;
using nexus::notify::Severity;

TEST_CASE("post stores a notification newest-first", "[notify][center]") {
    NotificationCenter center;
    const auto first = center.post("storage", Severity::Info, "scan started");
    const auto second = center.post("backup", Severity::Warning, "disk almost full");

    REQUIRE(center.size() == 2);
    const auto recent = center.recent();
    REQUIRE(recent.size() == 2);
    REQUIRE(recent[0].id == second);
    REQUIRE(recent[1].id == first);
    REQUIRE(recent[0].title == "disk almost full");
    REQUIRE(recent[0].severity == Severity::Warning);
    REQUIRE_FALSE(recent[0].created_at.time_since_epoch().count() == 0);
}

TEST_CASE("recent respects its limit", "[notify][center]") {
    NotificationCenter center;
    for (int i = 0; i < 10; ++i) {
        center.post("m", Severity::Info, "n" + std::to_string(i));
    }
    REQUIRE(center.recent(3).size() == 3);
    REQUIRE(center.recent(3)[0].title == "n9");
}

TEST_CASE("read tracking", "[notify][center]") {
    NotificationCenter center;
    const auto a = center.post("m", Severity::Info, "a");
    center.post("m", Severity::Error, "b");

    REQUIRE(center.unread_count() == 2);
    REQUIRE(center.mark_read(a));
    REQUIRE_FALSE(center.mark_read(a)); // already read
    REQUIRE(center.unread_count() == 1);
    REQUIRE(center.unread().size() == 1);
    REQUIRE(center.unread()[0].title == "b");

    REQUIRE(center.mark_all_read() == 1);
    REQUIRE(center.unread_count() == 0);
}

TEST_CASE("history is bounded and evicts oldest", "[notify][center]") {
    NotificationCenter center(3);
    for (int i = 0; i < 6; ++i) {
        center.post("m", Severity::Info, "n" + std::to_string(i));
    }
    REQUIRE(center.size() == 3);
    const auto recent = center.recent(10);
    REQUIRE(recent.front().title == "n5");
    REQUIRE(recent.back().title == "n3");
}

TEST_CASE("observers receive posts until unsubscribed", "[notify][center]") {
    NotificationCenter center;
    std::vector<std::string> seen;
    const auto sub = center.subscribe([&](const Notification& n) { seen.push_back(n.title); });

    center.post("m", Severity::Info, "one");
    center.post("m", Severity::Info, "two");
    REQUIRE(seen == std::vector<std::string>{"one", "two"});

    REQUIRE(center.unsubscribe(sub));
    REQUIRE_FALSE(center.unsubscribe(sub));
    center.post("m", Severity::Info, "three");
    REQUIRE(seen.size() == 2);
}

TEST_CASE("persist sink sees each new notification once", "[notify][center]") {
    NotificationCenter center;
    int calls = 0;
    nexus::core::Uuid last{};
    center.set_persist_sink([&](const Notification& n) {
        ++calls;
        last = n.id;
    });

    const auto id = center.post("m", Severity::Success, "done");
    REQUIRE(calls == 1);
    REQUIRE(last == id);

    center.set_persist_sink({});
    center.post("m", Severity::Info, "quiet");
    REQUIRE(calls == 1);
}
