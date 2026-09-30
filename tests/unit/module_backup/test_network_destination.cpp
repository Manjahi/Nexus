#include "nexus/module/backup/network_destination.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>

namespace fs = std::filesystem;
using namespace nexus::module::backup;

TEST_CASE("is_unc_destination recognises a \\\\host\\share prefix", "[backup][network]") {
    REQUIRE(is_unc_destination(R"(\\server\share\backups)"));
    REQUIRE(is_unc_destination(R"(\\192.168.1.10\backup)"));
    REQUIRE_FALSE(is_unc_destination(R"(C:\Users\me\backups)"));
    REQUIRE_FALSE(is_unc_destination("relative/path"));
    REQUIRE_FALSE(is_unc_destination(""));
}

TEST_CASE("check_destination_reachable accepts a real local folder", "[backup][network]") {
    const auto tag = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path dir = fs::temp_directory_path() / ("nexuspc_dest_" + std::to_string(tag));
    fs::create_directories(dir);

    const auto result = check_destination_reachable(dir.string());
    REQUIRE_FALSE(result.has_value());

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("check_destination_reachable flags a nonexistent local folder", "[backup][network]") {
    const auto tag = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path dir =
        fs::temp_directory_path() / ("nexuspc_dest_missing_" + std::to_string(tag));

    const auto result = check_destination_reachable(dir.string());
    REQUIRE(result.has_value());
}
