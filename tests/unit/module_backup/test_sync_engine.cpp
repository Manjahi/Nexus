#include "nexus/fs/exclusion_rules.hpp"
#include "nexus/module/backup/sync_engine.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

namespace fs = std::filesystem;
using namespace nexus::module::backup;

namespace {

struct Fixture {
    fs::path base;
    fs::path source;
    fs::path dest;

    Fixture() {
        const auto tag = std::chrono::steady_clock::now().time_since_epoch().count();
        base = fs::temp_directory_path() / ("nexuspc_sync_" + std::to_string(tag));
        source = base / "src";
        dest = base / "dest";
        fs::create_directories(source / "sub");
        write(source / "one.txt", "file one");
        write(source / "sub" / "two.txt", "file two");
        write(source / "sub" / "three.txt", "file three"); // keeps "sub" non-empty in the
                                                           // file-deletion test below
    }
    ~Fixture() {
        std::error_code ec;
        fs::remove_all(base, ec);
    }
    static void write(const fs::path& p, std::string_view content) {
        std::ofstream out(p, std::ios::binary);
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
    }
    static std::string read(const fs::path& p) {
        std::ifstream in(p, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
    }
};

} // namespace

TEST_CASE("a fresh sync copies every file into an empty destination", "[backup][sync]") {
    Fixture f;
    SyncEngine engine;
    const auto summary = engine.run(f.source, f.dest, nexus::fs::ExclusionRules{});

    CHECK(summary.files_copied == 3);
    CHECK(summary.files_deleted == 0);
    CHECK_FALSE(summary.cancelled);
    REQUIRE(fs::exists(f.dest / "one.txt"));
    REQUIRE(fs::exists(f.dest / "sub" / "two.txt"));
    CHECK(Fixture::read(f.dest / "one.txt") == "file one");
    CHECK(Fixture::read(f.dest / "sub" / "two.txt") == "file two");
}

TEST_CASE("re-running a sync with nothing changed copies nothing", "[backup][sync]") {
    Fixture f;
    SyncEngine engine;
    engine.run(f.source, f.dest, nexus::fs::ExclusionRules{});

    const auto second = engine.run(f.source, f.dest, nexus::fs::ExclusionRules{});
    CHECK(second.files_copied == 0);
    CHECK(second.files_deleted == 0);
}

TEST_CASE("a changed source file is re-copied", "[backup][sync]") {
    Fixture f;
    SyncEngine engine;
    engine.run(f.source, f.dest, nexus::fs::ExclusionRules{});

    // Ensure the new mtime is unambiguously later than the tolerance window
    // sync_engine.cpp uses to treat near-identical timestamps as unchanged.
    std::this_thread::sleep_for(std::chrono::seconds{3});
    Fixture::write(f.source / "one.txt", "file one, but different now");

    const auto second = engine.run(f.source, f.dest, nexus::fs::ExclusionRules{});
    CHECK(second.files_copied == 1);
    CHECK(Fixture::read(f.dest / "one.txt") == "file one, but different now");
}

TEST_CASE("a file removed from the source is removed from the destination", "[backup][sync]") {
    Fixture f;
    SyncEngine engine;
    engine.run(f.source, f.dest, nexus::fs::ExclusionRules{});
    REQUIRE(fs::exists(f.dest / "sub" / "two.txt"));

    fs::remove(f.source / "sub" / "two.txt");
    const auto second = engine.run(f.source, f.dest, nexus::fs::ExclusionRules{});

    CHECK(second.files_copied == 0);
    CHECK(second.files_deleted == 1);
    CHECK_FALSE(fs::exists(f.dest / "sub" / "two.txt"));
    // one.txt is untouched.
    CHECK(fs::exists(f.dest / "one.txt"));
}

TEST_CASE("an entire directory removed from the source is removed from the destination",
          "[backup][sync]") {
    Fixture f;
    SyncEngine engine;
    engine.run(f.source, f.dest, nexus::fs::ExclusionRules{});
    REQUIRE(fs::exists(f.dest / "sub"));

    fs::remove_all(f.source / "sub");
    const auto second = engine.run(f.source, f.dest, nexus::fs::ExclusionRules{});

    CHECK(second.dirs_deleted == 1);
    CHECK_FALSE(fs::exists(f.dest / "sub"));
    CHECK(fs::exists(f.dest / "one.txt"));
}
