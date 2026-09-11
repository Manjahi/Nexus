#include "nexus/vault/vault_file.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

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

TEST_CASE("create writes a vault and returns the derived key + empty entries",
         "[vault][file]") {
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
        std::fstream f(scratch.path, std::ios::binary | std::ios::in | std::ios::out);
        REQUIRE(f);
        f.seekp(-1, std::ios::end);
        f.put('\xFF');
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
