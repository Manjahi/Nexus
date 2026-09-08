#include "nexus/fs/walker.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <fstream>
#include <set>
#include <string>
#include <string_view>
#include <system_error>

namespace fs = std::filesystem;
using nexus::fs::ExclusionRules;
using nexus::fs::WalkOptions;

namespace {

struct TempTree {
    fs::path root;

    TempTree() {
        const auto tag = std::chrono::steady_clock::now().time_since_epoch().count();
        root = fs::temp_directory_path() / ("nexuspc_walk_" + std::to_string(tag));
        fs::create_directories(root / "src");
        fs::create_directories(root / "node_modules" / "pkg");
        fs::create_directories(root / "build");
        write(root / "readme.txt", "hello");
        write(root / "src" / "main.cpp", "int main(){}");
        write(root / "src" / "notes.tmp", "scratch");
        write(root / "node_modules" / "pkg" / "index.js", "x");
        write(root / "build" / "out.o", "obj");
    }
    ~TempTree() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    static void write(const fs::path& p, std::string_view content) {
        std::ofstream out(p, std::ios::binary);
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
    }
};

std::set<std::string> names(const fs::path& root, const ExclusionRules& rules,
                            const WalkOptions& opts) {
    std::set<std::string> found;
    nexus::fs::walk(root, rules, opts, [&](const nexus::fs::FileEntry& e) {
        found.insert(fs::relative(e.path, root).generic_string());
    });
    return found;
}

} // namespace

TEST_CASE("walk visits every regular file with no rules", "[fs][walker]") {
    TempTree tree;
    const auto stats = nexus::fs::walk(tree.root, {}, {}, nullptr);
    REQUIRE(stats.files == 5);
    REQUIRE(stats.directories == 4);
    REQUIRE(stats.bytes > 0);
    REQUIRE_FALSE(stats.cancelled);
}

TEST_CASE("directory-name exclusions prune whole subtrees", "[fs][walker]") {
    TempTree tree;
    ExclusionRules rules;
    rules.exclude_directory_name("node_modules");
    rules.exclude_directory_name("build");

    const auto found = names(tree.root, rules, {});
    REQUIRE(found == std::set<std::string>{"readme.txt", "src/main.cpp", "src/notes.tmp"});
}

TEST_CASE("glob exclusions drop matching files", "[fs][walker]") {
    TempTree tree;
    ExclusionRules rules;
    rules.exclude_glob("*.tmp");

    const auto found = names(tree.root, rules, {});
    REQUIRE(found.count("src/main.cpp") == 1);
    REQUIRE(found.count("src/notes.tmp") == 0);
}

TEST_CASE("max_depth limits recursion", "[fs][walker]") {
    TempTree tree;
    WalkOptions opts;
    opts.max_depth = 0; // only the root's direct children

    const auto found = names(tree.root, ExclusionRules::defaults(), opts);
    REQUIRE(found == std::set<std::string>{"readme.txt"});
}

TEST_CASE("cancellation stops the walk", "[fs][walker]") {
    TempTree tree;
    std::atomic<int> seen{0};
    const auto stats = nexus::fs::walk(
        tree.root, {}, {},
        [&](const nexus::fs::FileEntry&) { seen.fetch_add(1); },
        [&] { return seen.load() >= 1; });

    REQUIRE(stats.cancelled);
    REQUIRE(seen.load() <= 2);
}

TEST_CASE("walking a missing directory reports an error", "[fs][walker]") {
    const auto stats = nexus::fs::walk("C:/nexuspc/no/such/dir", {}, {}, nullptr);
    REQUIRE(stats.errors == 1);
    REQUIRE(stats.files == 0);
}
