#include "nexus/jobs/cancellation.hpp"

#include <catch2/catch_test_macros.hpp>

using nexus::jobs::CancellationSource;
using nexus::jobs::CancellationToken;
using nexus::jobs::OperationCancelled;

TEST_CASE("default token is never cancelled", "[jobs][cancellation]") {
    CancellationToken token;
    REQUIRE_FALSE(token.is_cancelled());
    REQUIRE_NOTHROW(token.throw_if_cancelled());
}

TEST_CASE("source drives its tokens", "[jobs][cancellation]") {
    CancellationSource source;
    CancellationToken token = source.token();

    REQUIRE_FALSE(token.is_cancelled());
    source.cancel();
    REQUIRE(source.cancelled());
    REQUIRE(token.is_cancelled());
    REQUIRE_THROWS_AS(token.throw_if_cancelled(), OperationCancelled);
}

TEST_CASE("token copies observe the same source", "[jobs][cancellation]") {
    CancellationSource source;
    CancellationToken a = source.token();
    CancellationToken b = a; // NOLINT(performance-unnecessary-copy-initialization)

    source.cancel();
    REQUIRE(a.is_cancelled());
    REQUIRE(b.is_cancelled());
}
