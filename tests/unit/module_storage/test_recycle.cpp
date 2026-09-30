#include "nexus/module/storage/recycle.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
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
// Previously tagged [.integration] and so silently excluded from every
// normal ctest run (including CI) by Catch2's own default hidden-tag
// behavior - the safety-critical recycle-vs-permanent-delete path had zero
// actual coverage anywhere. Runs by default now: it's fast (one small
// throwaway temp file), self-contained, and cleans up after itself, and
// GitHub Actions' windows-latest runners have a real enough session for
// IFileOperation to work (this project's other UI-automation-driven
// verification already relies on that).
TEST_CASE("a real file is moved to the Recycle Bin", "[storage][recycle]") {
    const auto tag = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path =
        fs::temp_directory_path() / ("nexuspc_recycle_" + std::to_string(tag) + ".txt");
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
