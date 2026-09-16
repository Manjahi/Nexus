#include "nexus/module/backup/object_store.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>

namespace fs = std::filesystem;
using nexus::module::backup::ObjectStore;

namespace {

struct Scratch {
    fs::path dir;
    Scratch() {
        const auto tag = std::chrono::steady_clock::now().time_since_epoch().count();
        dir = fs::temp_directory_path() / ("nexuspc_store_" + std::to_string(tag));
        fs::create_directories(dir);
    }
    ~Scratch() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    fs::path write(std::string_view name, std::string_view content) {
        const fs::path p = dir / name;
        std::ofstream out(p, std::ios::binary);
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        return p;
    }
    std::string read(const fs::path& p) {
        std::ifstream in(p, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
    }
};

} // namespace

TEST_CASE("put_file stores once and dedups thereafter", "[backup][store]") {
    Scratch s;
    ObjectStore store(s.dir / "objects");

    const auto a = store.put_file(s.write("a.txt", "identical content"));
    REQUIRE(a.has_value());
    REQUIRE(a->was_new);
    REQUIRE(a->size == 17);
    REQUIRE(store.contains(a->digest));

    const auto b = store.put_file(s.write("b.txt", "identical content"));
    REQUIRE(b.has_value());
    REQUIRE(b->digest == a->digest);
    REQUIRE_FALSE(b->was_new); // same content -> not written again

    const auto c = store.put_file(s.write("c.txt", "different content!"));
    REQUIRE(c->was_new);
    REQUIRE_FALSE(c->digest == a->digest);
}

TEST_CASE("extract_to recreates the original bytes", "[backup][store]") {
    Scratch s;
    ObjectStore store(s.dir / "objects");
    const auto put = store.put_file(s.write("src.bin", std::string(5000, 'q')));
    REQUIRE(put.has_value());

    const fs::path out = s.dir / "restored" / "src.bin";
    REQUIRE(store.extract_to(put->digest, out));
    REQUIRE(s.read(out) == std::string(5000, 'q'));
}

TEST_CASE("verify catches a corrupted blob", "[backup][store]") {
    Scratch s;
    ObjectStore store(s.dir / "objects");
    const auto put = store.put_file(s.write("x", "hello"));
    REQUIRE(put.has_value());
    REQUIRE(store.verify(put->digest));

    // Corrupt the stored blob in place.
    {
        std::ofstream out(store.path_for(put->digest), std::ios::binary | std::ios::trunc);
        out << "tampered";
    }
    REQUIRE_FALSE(store.verify(put->digest));
}

TEST_CASE("missing content is reported, not fatal", "[backup][store]") {
    Scratch s;
    ObjectStore store(s.dir / "objects");
    REQUIRE_FALSE(store.put_file(s.dir / "nope.txt").has_value());

    nexus::hash::Digest fake{};
    REQUIRE_FALSE(store.contains(fake));
    REQUIRE_FALSE(store.extract_to(fake, s.dir / "out"));
}

TEST_CASE("collect_garbage reclaims blobs no longer referenced", "[backup][store]") {
    Scratch s;
    ObjectStore store(s.dir / "objects");

    const auto kept = store.put_file(s.write("keep.txt", "still referenced"));
    const auto orphaned = store.put_file(s.write("gone.txt", "no longer referenced"));
    REQUIRE(kept.has_value());
    REQUIRE(orphaned.has_value());
    REQUIRE(store.contains(kept->digest));
    REQUIRE(store.contains(orphaned->digest));

    const auto result =
        store.collect_garbage({nexus::hash::to_hex(kept->digest)});

    REQUIRE(result.blobs_removed == 1);
    REQUIRE(result.bytes_reclaimed == orphaned->size);
    REQUIRE(store.contains(kept->digest));
    REQUIRE_FALSE(store.contains(orphaned->digest));
}

TEST_CASE("collect_garbage with an empty keep set clears everything", "[backup][store]") {
    Scratch s;
    ObjectStore store(s.dir / "objects");
    REQUIRE(store.put_file(s.write("a.txt", "a")).has_value());
    REQUIRE(store.put_file(s.write("b.txt", "b")).has_value());

    const auto result = store.collect_garbage({});
    REQUIRE(result.blobs_removed == 2);

    // Directory tree itself is left in place, just emptied of blobs and
    // now-empty shard subdirectories.
    REQUIRE(fs::exists(s.dir / "objects"));
    REQUIRE(fs::is_empty(s.dir / "objects"));
}
