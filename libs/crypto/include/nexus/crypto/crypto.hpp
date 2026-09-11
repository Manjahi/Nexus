#pragma once

#include <sodium.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nexus::crypto {

/// Initializes libsodium (idempotent, thread-safe). Every other function in
/// this library calls this internally, so callers only need it explicitly if
/// they touch libsodium APIs directly. Aborts the process if libsodium's
/// internal self-tests fail — there is no safe way to continue past that.
void ensure_initialized();

inline constexpr std::size_t kAeadKeyBytes = crypto_aead_xchacha20poly1305_ietf_KEYBYTES;
inline constexpr std::size_t kAeadNonceBytes = crypto_aead_xchacha20poly1305_ietf_NPUBBYTES;
inline constexpr std::size_t kAeadTagBytes = crypto_aead_xchacha20poly1305_ietf_ABYTES;
inline constexpr std::size_t kKdfSaltBytes = crypto_pwhash_SALTBYTES;

/// RAII buffer for secret material: allocated with sodium_malloc (guard
/// pages, non-swappable best-effort), mlock'd, and zeroed on destruction or
/// move-from. Never copyable — secrets should have exactly one owner.
class SecureBuffer {
public:
    explicit SecureBuffer(std::size_t size);
    ~SecureBuffer();

    SecureBuffer(SecureBuffer&& other) noexcept;
    SecureBuffer& operator=(SecureBuffer&& other) noexcept;
    SecureBuffer(const SecureBuffer&) = delete;
    SecureBuffer& operator=(const SecureBuffer&) = delete;

    [[nodiscard]] std::uint8_t* data() noexcept { return static_cast<std::uint8_t*>(ptr_); }
    [[nodiscard]] const std::uint8_t* data() const noexcept {
        return static_cast<const std::uint8_t*>(ptr_);
    }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::span<std::uint8_t> span() noexcept { return {data(), size_}; }
    [[nodiscard]] std::span<const std::uint8_t> span() const noexcept { return {data(), size_}; }

private:
    void release() noexcept;

    void* ptr_ = nullptr;
    std::size_t size_ = 0;
};

/// Fills `out` with cryptographically secure random bytes.
void random_bytes(std::span<std::uint8_t> out);
[[nodiscard]] std::vector<std::uint8_t> random_bytes(std::size_t count);

struct KdfParams {
    std::uint64_t opslimit = 0;
    std::size_t memlimit = 0;

    /// libsodium's Argon2id "interactive" profile: tuned to unlock in well
    /// under a second on typical hardware while still being expensive to
    /// brute-force offline.
    [[nodiscard]] static KdfParams interactive();
};

/// Derives `key_len` bytes from `password` and a `kKdfSaltBytes`-byte `salt`
/// via Argon2id. Returns nullopt only on local resource exhaustion (e.g. the
/// requested memlimit can't be allocated) — never on a "wrong" password,
/// since the KDF has no way to know that.
[[nodiscard]] std::optional<SecureBuffer> derive_key(std::string_view password,
                                                     std::span<const std::uint8_t> salt,
                                                     std::size_t key_len,
                                                     const KdfParams& params = KdfParams::interactive());

/// Seals `plaintext` with XChaCha20-Poly1305. `key` must be `kAeadKeyBytes`
/// long and `nonce` `kAeadNonceBytes` long; `associated_data` is
/// authenticated but not encrypted (e.g. a format header). The nonce must
/// never be reused with the same key.
[[nodiscard]] std::vector<std::uint8_t> aead_encrypt(std::span<const std::uint8_t> key,
                                                     std::span<const std::uint8_t> nonce,
                                                     std::span<const std::uint8_t> plaintext,
                                                     std::span<const std::uint8_t> associated_data = {});

/// Opens a blob sealed by aead_encrypt. Returns nullopt if authentication
/// fails (wrong key, wrong associated data, or corrupted/tampered bytes).
[[nodiscard]] std::optional<std::vector<std::uint8_t>> aead_decrypt(
    std::span<const std::uint8_t> key, std::span<const std::uint8_t> nonce,
    std::span<const std::uint8_t> ciphertext, std::span<const std::uint8_t> associated_data = {});

struct PasswordPolicy {
    std::size_t length = 20;
    bool lowercase = true;
    bool uppercase = true;
    bool digits = true;
    bool symbols = true;
};

/// Generates a password from `policy` using rejection-sampled random bytes
/// (no modulo bias). At least one character class must be enabled and
/// length must be > 0; violating either returns an empty string.
[[nodiscard]] std::string generate_password(const PasswordPolicy& policy = {});

/// Constant-time comparison (no early-exit on mismatch), for anything that
/// compares secret bytes (derived keys, MACs handled outside the AEAD API).
[[nodiscard]] bool constant_time_equal(std::span<const std::uint8_t> a,
                                       std::span<const std::uint8_t> b) noexcept;

} // namespace nexus::crypto
