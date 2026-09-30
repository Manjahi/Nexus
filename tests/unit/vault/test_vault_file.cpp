#include "nexus/vault/vault_file.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;
using namespace nexus::vault;

namespace {

struct Scratch {
    fs::path path;
    Scratch() {
        const auto tag = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() / ("nexuspc_vault_" + std::to_string(tag) + ".nxv");
    }
    ~Scratch() {
        std::error_code ec;
        fs::remove(path, ec);
        fs::remove(fs::path(path.string() + ".tmp"), ec);
    }
};

// Cheap KDF params so tests don't pay the full ~0.5-1s interactive cost per
// derive_key call - correctness of the format doesn't depend on the profile.
nexus::crypto::KdfParams cheap_params() {
    return nexus::crypto::KdfParams{/*opslimit=*/1, /*memlimit=*/8 * 1024 * 1024};
}

} // namespace

TEST_CASE("create writes a vault and returns the derived key + empty entries", "[vault][file]") {
    Scratch scratch;
    REQUIRE_FALSE(VaultFile::exists(scratch.path));

    const auto created = VaultFile::create(scratch.path, "hunter2", cheap_params());
    REQUIRE(created.has_value());
    REQUIRE(created->entries.empty());
    REQUIRE(created->key.size() == nexus::crypto::kAeadKeyBytes);
    REQUIRE(VaultFile::exists(scratch.path));
}

TEST_CASE("create fails if the file already exists", "[vault][file]") {
    Scratch scratch;
    REQUIRE(VaultFile::create(scratch.path, "hunter2", cheap_params()).has_value());
    REQUIRE_FALSE(VaultFile::create(scratch.path, "hunter2", cheap_params()).has_value());
}

TEST_CASE("unlock with the right password recovers saved entries", "[vault][file]") {
    Scratch scratch;
    const auto created = VaultFile::create(scratch.path, "hunter2", cheap_params());
    REQUIRE(created.has_value());

    Entry e;
    e.id = "1";
    e.title = "Router";
    e.password = "admin123";
    e.created_at = nexus::core::now();
    e.updated_at = e.created_at;

    REQUIRE(VaultFile::save(scratch.path, created->header, created->key.span(), {e}));

    const auto unlocked = VaultFile::unlock(scratch.path, "hunter2");
    REQUIRE(unlocked.has_value());
    REQUIRE(unlocked->entries.size() == 1);
    REQUIRE(unlocked->entries[0].title == "Router");
    REQUIRE(unlocked->entries[0].password == "admin123");
}

TEST_CASE("unlock with the wrong password fails", "[vault][file]") {
    Scratch scratch;
    REQUIRE(VaultFile::create(scratch.path, "hunter2", cheap_params()).has_value());
    REQUIRE_FALSE(VaultFile::unlock(scratch.path, "wrong password").has_value());
}

TEST_CASE("unlock of a nonexistent file fails", "[vault][file]") {
    Scratch scratch;
    REQUIRE_FALSE(VaultFile::unlock(scratch.path, "hunter2").has_value());
}

TEST_CASE("read_header succeeds without a password", "[vault][file]") {
    Scratch scratch;
    const auto created = VaultFile::create(scratch.path, "hunter2", cheap_params());
    REQUIRE(created.has_value());

    const auto header = VaultFile::read_header(scratch.path);
    REQUIRE(header.has_value());
    REQUIRE(header->format_version == 1);
    REQUIRE(header->salt.size() == nexus::crypto::kKdfSaltBytes);
}

TEST_CASE("a tampered vault file fails to unlock", "[vault][file]") {
    Scratch scratch;
    REQUIRE(VaultFile::create(scratch.path, "hunter2", cheap_params()).has_value());

    {
        // XOR-flip the last byte rather than overwrite with a fixed value:
        // the AEAD tag's last byte is random, so a fixed overwrite is a ~1/256
        // no-op (and thus a flaky test) whenever it already matches.
        std::fstream f(scratch.path, std::ios::binary | std::ios::in | std::ios::out);
        REQUIRE(f);
        f.seekg(-1, std::ios::end);
        char last = 0;
        f.get(last);
        f.seekp(-1, std::ios::end);
        f.put(static_cast<char>(static_cast<unsigned char>(last) ^ 0xFF));
    }

    REQUIRE_FALSE(VaultFile::unlock(scratch.path, "hunter2").has_value());
}

TEST_CASE("save re-seals under the same key across multiple calls", "[vault][file]") {
    Scratch scratch;
    const auto created = VaultFile::create(scratch.path, "hunter2", cheap_params());
    REQUIRE(created.has_value());

    Entry e1;
    e1.id = "1";
    e1.title = "First";
    REQUIRE(VaultFile::save(scratch.path, created->header, created->key.span(), {e1}));

    Entry e2;
    e2.id = "2";
    e2.title = "Second";
    REQUIRE(VaultFile::save(scratch.path, created->header, created->key.span(), {e1, e2}));

    const auto unlocked = VaultFile::unlock(scratch.path, "hunter2");
    REQUIRE(unlocked.has_value());
    REQUIRE(unlocked->entries.size() == 2);
}

// docs/security/vault-threat-model.md's review checklist: "Fuzz the vault
// file parser (it will be handed attacker-controlled bytes if a vault file
// is restored from an untrusted backup)." Structural edge cases first
// (deterministic regressions for the exact boundaries parse_file checks),
// then randomized mutation of a real vault file's bytes: the only contract
// under test is that malformed/truncated/corrupted input is rejected
// (nullopt) rather than crashing or throwing - never that any particular
// mutation is rejected, since a small fraction of random mutations could
// coincidentally still satisfy the header's structural checks.
TEST_CASE("read_header and unlock reject malformed files without crashing", "[vault][file][fuzz]") {
    Scratch scratch;
    const auto created = VaultFile::create(scratch.path, "hunter2", cheap_params());
    REQUIRE(created.has_value());

    Entry e;
    e.id = "1";
    e.title = "Router";
    e.password = "admin123";
    e.created_at = nexus::core::now();
    e.updated_at = e.created_at;
    REQUIRE(VaultFile::save(scratch.path, created->header, created->key.span(), {e}));

    std::vector<std::uint8_t> valid_bytes;
    {
        std::ifstream in(scratch.path, std::ios::binary | std::ios::ate);
        REQUIRE(in);
        const std::streamsize size = in.tellg();
        in.seekg(0);
        valid_bytes.resize(static_cast<std::size_t>(size));
        REQUIRE(in.read(reinterpret_cast<char*>(valid_bytes.data()), size));
    }
    REQUIRE_FALSE(valid_bytes.empty());

    auto write_bytes = [&](const std::vector<std::uint8_t>& bytes) {
        std::ofstream out(scratch.path, std::ios::binary | std::ios::trunc);
        REQUIRE(out);
        if (!bytes.empty()) {
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
        }
    };

    SECTION("structural edge cases") {
        write_bytes({});
        CHECK_FALSE(VaultFile::read_header(scratch.path).has_value());
        CHECK_FALSE(VaultFile::unlock(scratch.path, "hunter2").has_value());

        write_bytes({valid_bytes.begin(), valid_bytes.begin() + 8}); // magic only
        CHECK_FALSE(VaultFile::read_header(scratch.path).has_value());

        write_bytes({valid_bytes.begin(), valid_bytes.begin() + 20}); // truncated mid-header
        CHECK_FALSE(VaultFile::read_header(scratch.path).has_value());

        // header + salt complete, but cut one byte into the nonce
        const std::size_t header_len =
            8 + 4 + 8 + 8 + 4 + nexus::crypto::kKdfSaltBytes; // magic+ver+ops+mem+len+salt
        REQUIRE(header_len < valid_bytes.size());
        write_bytes({valid_bytes.begin(),
                     valid_bytes.begin() + static_cast<std::ptrdiff_t>(header_len + 1)});
        CHECK_FALSE(VaultFile::read_header(scratch.path).has_value());
        CHECK_FALSE(VaultFile::unlock(scratch.path, "hunter2").has_value());

        // valid header/nonce but ciphertext shorter than the AEAD tag
        auto short_ciphertext = valid_bytes;
        short_ciphertext.resize(short_ciphertext.size() > 4 ? short_ciphertext.size() - 4 : 0);
        write_bytes(short_ciphertext);
        CHECK_FALSE(VaultFile::unlock(scratch.path, "hunter2").has_value());

        // claimed salt length (the u32 right after magic+version+ops+mem) wildly
        // exceeds what's actually in the file
        auto bogus_salt_len = valid_bytes;
        constexpr std::size_t kSaltLenOffset = 8 + 4 + 8 + 8;
        bogus_salt_len[kSaltLenOffset] = 0xFF;
        bogus_salt_len[kSaltLenOffset + 1] = 0xFF;
        bogus_salt_len[kSaltLenOffset + 2] = 0xFF;
        bogus_salt_len[kSaltLenOffset + 3] = 0xFF;
        write_bytes(bogus_salt_len);
        CHECK_FALSE(VaultFile::read_header(scratch.path).has_value());
    }

    SECTION("random mutation of a real vault file never crashes or throws") {
        std::mt19937_64 rng(0xC0FFEEULL); // fixed seed: deterministic, reproducible on failure
        std::uniform_int_distribution<int> strategy(0, 3);
        std::uniform_int_distribution<int> byte_dist(0, 255);

        auto mutate = [&] {
            std::vector<std::uint8_t> mutated = valid_bytes;
            const std::size_t original_size = mutated.size();
            std::uniform_int_distribution<std::size_t> pos(0, original_size - 1);

            switch (strategy(rng)) {
                case 0: { // flip a handful of random bytes
                    const int flips = 1 + static_cast<int>(rng() % 8);
                    for (int f = 0; f < flips; ++f) {
                        mutated[pos(rng)] ^= static_cast<std::uint8_t>(byte_dist(rng));
                    }
                    break;
                }
                case 1: { // truncate to a random shorter (possibly zero) length
                    std::uniform_int_distribution<std::size_t> len(0, original_size);
                    mutated.resize(len(rng));
                    break;
                }
                case 2: { // append random trailing garbage
                    std::uniform_int_distribution<int> extra(1, 64);
                    const int n = extra(rng);
                    for (int k = 0; k < n; ++k) {
                        mutated.push_back(static_cast<std::uint8_t>(byte_dist(rng)));
                    }
                    break;
                }
                default: { // overwrite a random contiguous range
                    const std::size_t start = pos(rng);
                    const std::size_t len =
                        std::min<std::size_t>(original_size - start, 1 + (rng() % 16));
                    for (std::size_t k = 0; k < len; ++k) {
                        mutated[start + k] = static_cast<std::uint8_t>(byte_dist(rng));
                    }
                    break;
                }
            }
            return mutated;
        };

        // read_header() only ever runs parse_file() - no KDF, no AEAD - so it's
        // cheap enough to mutate heavily. This is the function that's handed
        // fully untrusted bytes (e.g. VaultClient probing a path before ever
        // asking for a password), and the one this test cares about most.
        for (int iteration = 0; iteration < 3000; ++iteration) {
            write_bytes(mutate());
            try {
                (void) VaultFile::read_header(scratch.path);
            } catch (const std::exception& ex) {
                FAIL("read_header threw on malformed input at iteration " << iteration << ": "
                                                                          << ex.what());
            }
        }

        // unlock() additionally runs the KDF once parse_file()'s (now bounds-
        // checked - see kMaxAcceptedOpslimit/kMaxAcceptedMemlimit in
        // vault_file.cpp) opslimit/memlimit pass, so a mutation that keeps the
        // header structurally valid can make each call cost real CPU time -
        // far fewer iterations here, enough to sample that path without
        // making this test slow.
        for (int iteration = 0; iteration < 150; ++iteration) {
            write_bytes(mutate());
            try {
                (void) VaultFile::unlock(scratch.path, "hunter2");
            } catch (const std::exception& ex) {
                FAIL("unlock threw on malformed input at iteration " << iteration << ": "
                                                                     << ex.what());
            }
        }
    }
}
