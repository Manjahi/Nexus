#include "nexus/module/storage/recycle.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using nexus::module::storage::recycle_to_bin;

TEST_CASE("recycling nothing is a no-op", "[storage][recycle]") {
    const auto result = recycle_to_bin({});
    REQUIRE(result.requested == 0);
    REQUIRE(result.recycled == 0);
    REQUIRE(result.ok());
}

TEST_CASE("a missing path is reported as failed", "[storage][recycle]") {
    const std::array<fs::path, 1> paths{fs::path("C:/nexuspc/definitely/not/here.bin")};
    const auto result = recycle_to_bin(paths);
    REQUIRE(result.requested == 1);
    REQUIRE(result.recycled == 0);
    REQUIRE(result.failed.size() == 1);
    REQUIRE_FALSE(result.ok());
}

#if defined(_WIN32)
TEST_CASE("a real file is moved to the Recycle Bin", "[storage][recycle][.integration]") {
    const auto tag = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = fs::temp_directory_path() / ("nexuspc_recycle_" + std::to_string(tag) + ".txt");
    {
        std::ofstream out(path, std::ios::binary);
        out << "throwaway";
    }
    REQUIRE(fs::exists(path));

    const std::array<fs::path, 1> paths{path};
    const auto result = recycle_to_bin(paths);

    REQUIRE(result.recycled == 1);
    REQUIRE(result.ok());
    REQUIRE_FALSE(fs::exists(path));
}
#endif
