#include "nexus/hash/hash.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>

using nexus::hash::Algorithm;
using nexus::hash::Digest;
using nexus::hash::Hasher;

namespace {

std::span<const std::byte> bytes_of(std::string_view s) {
    return std::as_bytes(std::span(s.data(), s.size()));
}

std::filesystem::path temp_file(std::string_view content) {
    const auto tag = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() /
                ("nexuspc_hash_" + std::to_string(tag) + ".bin");
    std::ofstream out(path, std::ios::binary);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    return path;
}

} // namespace

TEST_CASE("digests are 32 bytes and algorithm-dependent", "[hash]") {
    const auto b3 = nexus::hash::hash_bytes(bytes_of("nexuspc"), Algorithm::Blake3);
    const auto sha = nexus::hash::hash_bytes(bytes_of("nexuspc"), Algorithm::Sha256);
    REQUIRE(b3.size() == 32);
    REQUIRE(sha.size() == 32);
    REQUIRE(b3 != sha);
    REQUIRE(nexus::hash::to_hex(b3).size() == 64);
}

TEST_CASE("hex round-trips", "[hash]") {
    const Digest d = nexus::hash::hash_bytes(bytes_of("nexuspc"), Algorithm::Blake3);
    const auto parsed = nexus::hash::digest_from_hex(nexus::hash::to_hex(d));
    REQUIRE(parsed.has_value());
    REQUIRE(*parsed == d);
    REQUIRE_FALSE(nexus::hash::digest_from_hex("tooshort").has_value());
    REQUIRE_FALSE(nexus::hash::digest_from_hex(std::string(64, 'z')).has_value());
}

TEST_CASE("incremental updates match one-shot", "[hash]") {
    for (const auto algo : {Algorithm::Blake3, Algorithm::Sha256}) {
        Hasher h(algo);
        h.update(bytes_of("the quick "));
        h.update(bytes_of("brown fox"));
        REQUIRE(h.finish() == nexus::hash::hash_bytes(bytes_of("the quick brown fox"), algo));
    }
}

TEST_CASE("hash_file matches hashing the same bytes", "[hash]") {
    const std::string content(50'000, 'x');
    const auto path = temp_file(content);

    const auto file_digest = nexus::hash::hash_file(path, Algorithm::Blake3);
    REQUIRE(file_digest.has_value());
    REQUIRE(*file_digest == nexus::hash::hash_bytes(bytes_of(content), Algorithm::Blake3));

    std::filesystem::remove(path);
}

TEST_CASE("hash_file_prefix only reads the prefix", "[hash]") {
    const std::string content = std::string(1000, 'a') + std::string(1000, 'b');
    const auto path = temp_file(content);

    const auto prefix = nexus::hash::hash_file_prefix(path, 1000, Algorithm::Blake3);
    REQUIRE(prefix.has_value());
    REQUIRE(*prefix ==
            nexus::hash::hash_bytes(bytes_of(std::string(1000, 'a')), Algorithm::Blake3));

    // Asking for more bytes than the file has is fine.
    const auto whole = nexus::hash::hash_file_prefix(path, 1'000'000, Algorithm::Blake3);
    REQUIRE(whole == nexus::hash::hash_file(path, Algorithm::Blake3));

    std::filesystem::remove(path);
}

TEST_CASE("hash_file reports missing files", "[hash]") {
    REQUIRE_FALSE(nexus::hash::hash_file("C:/nexuspc/does/not/exist.bin").has_value());
}
