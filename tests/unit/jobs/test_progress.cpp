#include "nexus/jobs/progress.hpp"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <vector>

using nexus::jobs::Progress;
using nexus::jobs::ProgressReporter;

TEST_CASE("fraction is clamped to [0, 1]", "[jobs][progress]") {
    ProgressReporter reporter;
    reporter.set_fraction(-0.5);
    REQUIRE(reporter.snapshot().fraction == 0.0);
    reporter.set_fraction(1.5);
    REQUIRE(reporter.snapshot().fraction == 1.0);
    reporter.set_fraction(0.25);
    REQUIRE(reporter.snapshot().fraction == 0.25);
}

TEST_CASE("update sets fraction and message together", "[jobs][progress]") {
    ProgressReporter reporter;
    reporter.update(0.4, "hashing");
    const Progress p = reporter.snapshot();
    REQUIRE(p.fraction == 0.4);
    REQUIRE(p.message == "hashing");
}

TEST_CASE("sink receives every update", "[jobs][progress]") {
    std::vector<Progress> seen;
    ProgressReporter reporter([&](const Progress& p) { seen.push_back(p); });

    reporter.set_message("start");
    reporter.set_fraction(0.5);
    reporter.update(1.0, "done");

    REQUIRE(seen.size() == 3);
    REQUIRE(seen.back().fraction == 1.0);
    REQUIRE(seen.back().message == "done");
}

TEST_CASE("concurrent updates do not race", "[jobs][progress]") {
    std::atomic<int> calls{0};
    ProgressReporter reporter([&](const Progress&) { calls.fetch_add(1); });

    std::vector<std::jthread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&] {
            for (int i = 0; i < 250; ++i) {
                reporter.update(static_cast<double>(i) / 250.0, "tick");
            }
        });
    }
    threads.clear(); // join

    REQUIRE(calls.load() == 4 * 250);
    REQUIRE(reporter.snapshot().fraction <= 1.0);
}
