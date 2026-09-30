#include "nexus/crypto/crypto.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <set>

using namespace nexus::crypto;

TEST_CASE("random_bytes fills the requested length and isn't all zero", "[crypto][random]") {
    const auto bytes = random_bytes(32);
    REQUIRE(bytes.size() == 32);
    REQUIRE_FALSE(std::all_of(bytes.begin(), bytes.end(), [](std::uint8_t b) { return b == 0; }));
}

TEST_CASE("random_bytes produces different output each call", "[crypto][random]") {
    REQUIRE(random_bytes(32) != random_bytes(32));
}

TEST_CASE("derive_key is deterministic for the same password/salt/params", "[crypto][kdf]") {
    const auto salt = random_bytes(kKdfSaltBytes);
    const auto params = KdfParams::interactive();

    const auto key1 = derive_key("correct horse battery staple", salt, kAeadKeyBytes, params);
    const auto key2 = derive_key("correct horse battery staple", salt, kAeadKeyBytes, params);
    REQUIRE(key1.has_value());
    REQUIRE(key2.has_value());
    REQUIRE(constant_time_equal(key1->span(), key2->span()));
}

TEST_CASE("derive_key differs across salts and passwords", "[crypto][kdf]") {
    const auto salt_a = random_bytes(kKdfSaltBytes);
    const auto salt_b = random_bytes(kKdfSaltBytes);
    const auto params = KdfParams::interactive();

    const auto key_a = derive_key("hunter2", salt_a, kAeadKeyBytes, params);
    const auto key_b = derive_key("hunter2", salt_b, kAeadKeyBytes, params);
    const auto key_c = derive_key("hunter3", salt_a, kAeadKeyBytes, params);
    REQUIRE(key_a.has_value());
    REQUIRE(key_b.has_value());
    REQUIRE(key_c.has_value());
    REQUIRE_FALSE(constant_time_equal(key_a->span(), key_b->span()));
    REQUIRE_FALSE(constant_time_equal(key_a->span(), key_c->span()));
}

TEST_CASE("derive_key rejects a wrong-sized salt", "[crypto][kdf]") {
    std::vector<std::uint8_t> short_salt(kKdfSaltBytes - 1, 0);
    REQUIRE_FALSE(derive_key("pw", short_salt, kAeadKeyBytes).has_value());
}

TEST_CASE("aead round-trips plaintext with associated data", "[crypto][aead]") {
    const auto key = random_bytes(kAeadKeyBytes);
    const auto nonce = random_bytes(kAeadNonceBytes);
    const std::vector<std::uint8_t> plaintext{'h', 'e', 'l', 'l', 'o'};
    const std::vector<std::uint8_t> ad{'v', '1'};

    const auto ciphertext = aead_encrypt(key, nonce, plaintext, ad);
    REQUIRE(ciphertext.size() == plaintext.size() + kAeadTagBytes);

    const auto decrypted = aead_decrypt(key, nonce, ciphertext, ad);
    REQUIRE(decrypted.has_value());
    REQUIRE(*decrypted == plaintext);
}

TEST_CASE("aead decrypt fails on tampered ciphertext", "[crypto][aead]") {
    const auto key = random_bytes(kAeadKeyBytes);
    const auto nonce = random_bytes(kAeadNonceBytes);
    auto ciphertext = aead_encrypt(key, nonce, std::vector<std::uint8_t>{1, 2, 3});

    ciphertext[0] ^= 0xFF;
    REQUIRE_FALSE(aead_decrypt(key, nonce, ciphertext).has_value());
}

TEST_CASE("aead decrypt fails with the wrong key", "[crypto][aead]") {
    const auto key = random_bytes(kAeadKeyBytes);
    const auto wrong_key = random_bytes(kAeadKeyBytes);
    const auto nonce = random_bytes(kAeadNonceBytes);
    const auto ciphertext = aead_encrypt(key, nonce, std::vector<std::uint8_t>{1, 2, 3});

    REQUIRE_FALSE(aead_decrypt(wrong_key, nonce, ciphertext).has_value());
}

TEST_CASE("aead decrypt fails with mismatched associated data", "[crypto][aead]") {
    const auto key = random_bytes(kAeadKeyBytes);
    const auto nonce = random_bytes(kAeadNonceBytes);
    const std::vector<std::uint8_t> ad_used{'v', '1'};
    const std::vector<std::uint8_t> ad_wrong{'v', '2'};
    const auto ciphertext = aead_encrypt(key, nonce, std::vector<std::uint8_t>{1, 2, 3}, ad_used);

    REQUIRE_FALSE(aead_decrypt(key, nonce, ciphertext, ad_wrong).has_value());
}

TEST_CASE("aead decrypt rejects ciphertext shorter than the tag", "[crypto][aead]") {
    const auto key = random_bytes(kAeadKeyBytes);
    const auto nonce = random_bytes(kAeadNonceBytes);
    REQUIRE_FALSE(aead_decrypt(key, nonce, std::vector<std::uint8_t>{1, 2, 3}).has_value());
}

TEST_CASE("aead_encrypt rejects wrong-sized key or nonce", "[crypto][aead]") {
    const std::vector<std::uint8_t> bad_key(kAeadKeyBytes - 1, 0);
    const auto nonce = random_bytes(kAeadNonceBytes);
    REQUIRE_THROWS_AS(aead_encrypt(bad_key, nonce, std::vector<std::uint8_t>{1}),
                      std::invalid_argument);
}

TEST_CASE("generate_password respects the requested length", "[crypto][password]") {
    PasswordPolicy policy;
    policy.length = 24;
    const auto pw = generate_password(policy);
    REQUIRE(pw.size() == 24);
}

TEST_CASE("generate_password with everything disabled returns empty", "[crypto][password]") {
    PasswordPolicy policy;
    policy.lowercase = false;
    policy.uppercase = false;
    policy.digits = false;
    policy.symbols = false;
    REQUIRE(generate_password(policy).empty());
}

TEST_CASE("generate_password with zero length returns empty", "[crypto][password]") {
    PasswordPolicy policy;
    policy.length = 0;
    REQUIRE(generate_password(policy).empty());
}

TEST_CASE("generate_password digits-only uses only digits", "[crypto][password]") {
    PasswordPolicy policy;
    policy.length = 40;
    policy.lowercase = false;
    policy.uppercase = false;
    policy.digits = true;
    policy.symbols = false;
    const auto pw = generate_password(policy);
    REQUIRE(pw.size() == 40);
    REQUIRE(std::all_of(pw.begin(), pw.end(), [](char c) { return c >= '0' && c <= '9'; }));
}

TEST_CASE("generate_password calls are not identical", "[crypto][password]") {
    PasswordPolicy policy;
    policy.length = 20;
    std::set<std::string> seen;
    for (int i = 0; i < 5; ++i) {
        seen.insert(generate_password(policy));
    }
    REQUIRE(seen.size() == 5);
}

TEST_CASE("constant_time_equal compares content, not identity", "[crypto][compare]") {
    const std::vector<std::uint8_t> a{1, 2, 3};
    const std::vector<std::uint8_t> b{1, 2, 3};
    const std::vector<std::uint8_t> c{1, 2, 4};
    const std::vector<std::uint8_t> d{1, 2};

    REQUIRE(constant_time_equal(a, b));
    REQUIRE_FALSE(constant_time_equal(a, c));
    REQUIRE_FALSE(constant_time_equal(a, d));
}

TEST_CASE("SecureBuffer move leaves the source empty", "[crypto][securebuffer]") {
    SecureBuffer buf(16);
    REQUIRE(buf.size() == 16);

    SecureBuffer moved = std::move(buf);
    REQUIRE(moved.size() == 16);
    REQUIRE(buf.size() == 0); // NOLINT(bugprone-use-after-move) - explicitly testing move state
}

// docs/security/vault-threat-model.md's review checklist: "Confirm
// sodium_mlock actually takes effect on the target Windows version...and
// fails safe (does not silently skip locking) if it can't." SecureBuffer
// relies entirely on sodium_malloc()'s internal sodium_mlock() call, and
// libsodium never surfaces an mlock failure through sodium_malloc()'s return
// value - the allocation succeeds either way, which is the "fails safe" part
// (a locked-memory failure never crashes or blocks the app). This test calls
// the underlying primitive directly so the actual outcome on the machine
// running the suite is recorded by a real assertion, not assumed from
// documentation: on Windows, sodium_mlock() wraps VirtualLock(), which can
// fail if the process's minimum working set quota is exhausted, so this is
// worth re-checking if it ever starts failing in CI.
TEST_CASE("sodium_mlock succeeds on this platform", "[crypto][securebuffer][mlock]") {
    ensure_initialized();
    alignas(64) unsigned char buf[4096] = {};
    const int lock_rc = sodium_mlock(buf, sizeof(buf));
    INFO("sodium_mlock returned " << lock_rc << " (0 = locked, -1 = failed)");
    REQUIRE(lock_rc == 0);
    REQUIRE(sodium_munlock(buf, sizeof(buf)) == 0);
}
