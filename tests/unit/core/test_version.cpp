#include "nexus/core/version.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string_view>

TEST_CASE("version constants are populated from CMake", "[core][version]") {
    STATIC_REQUIRE(nexus::core::version_major >= 0);
    STATIC_REQUIRE(nexus::core::version_minor >= 0);
    STATIC_REQUIRE(nexus::core::version_patch >= 0);

    const std::string_view s{nexus::core::version_string};
    REQUIRE_FALSE(s.empty());
    REQUIRE(s.find('.') != std::string_view::npos);
}
