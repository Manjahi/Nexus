#include "nexus/crypto/crypto.hpp"

#include <mutex>
#include <new>
#include <stdexcept>

namespace nexus::crypto {

void ensure_initialized() {
    static std::once_flag flag;
    std::call_once(flag, [] {
        if (sodium_init() < 0) {
            // libsodium couldn't verify its own primitives; there is no safe
            // way to continue running crypto code past this point.
            std::abort();
        }
    });
}

SecureBuffer::SecureBuffer(std::size_t size) : size_(size) {
    ensure_initialized();
    // sodium_malloc(0) is technically allowed but returns a zero-length
    // region with no useful guard-page behavior; round up so callers can
    // always dereference data() safely even for a nominally empty buffer.
    ptr_ = sodium_malloc(size_ == 0 ? 1 : size_);
    if (ptr_ == nullptr) {
        throw std::bad_alloc();
    }
    sodium_memzero(ptr_, size_ == 0 ? 1 : size_);
}

void SecureBuffer::release() noexcept {
    if (ptr_ != nullptr) {
        sodium_free(ptr_); // wipes the region before returning it to the OS
        ptr_ = nullptr;
        size_ = 0;
    }
}

SecureBuffer::~SecureBuffer() { release(); }

SecureBuffer::SecureBuffer(SecureBuffer&& other) noexcept : ptr_(other.ptr_), size_(other.size_) {
    other.ptr_ = nullptr;
    other.size_ = 0;
}

SecureBuffer& SecureBuffer::operator=(SecureBuffer&& other) noexcept {
    if (this != &other) {
        release();
        ptr_ = other.ptr_;
        size_ = other.size_;
        other.ptr_ = nullptr;
        other.size_ = 0;
    }
    return *this;
}

void random_bytes(std::span<std::uint8_t> out) {
    ensure_initialized();
    randombytes_buf(out.data(), out.size());
}

std::vector<std::uint8_t> random_bytes(std::size_t count) {
    std::vector<std::uint8_t> out(count);
    random_bytes(out);
    return out;
}

KdfParams KdfParams::interactive() {
    ensure_initialized();
    return KdfParams{crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE};
}

std::optional<SecureBuffer> derive_key(std::string_view password, std::span<const std::uint8_t> salt,
                                       std::size_t key_len, const KdfParams& params) {
    ensure_initialized();
    if (salt.size() != kKdfSaltBytes || key_len == 0) {
        return std::nullopt;
    }

    SecureBuffer key(key_len);
    const int rc = crypto_pwhash(
        key.data(), key_len, reinterpret_cast<const char*>(password.data()), password.size(),
        salt.data(), params.opslimit, params.memlimit, crypto_pwhash_ALG_ARGON2ID13);
    if (rc != 0) {
        // Only fails on local resource exhaustion (e.g. memlimit can't be
        // allocated); a "wrong" password still produces a (wrong) key.
        return std::nullopt;
    }
    return key;
}

std::vector<std::uint8_t> aead_encrypt(std::span<const std::uint8_t> key,
                                       std::span<const std::uint8_t> nonce,
                                       std::span<const std::uint8_t> plaintext,
                                       std::span<const std::uint8_t> associated_data) {
    ensure_initialized();
    if (key.size() != kAeadKeyBytes || nonce.size() != kAeadNonceBytes) {
        throw std::invalid_argument("aead_encrypt: wrong key or nonce length");
    }

    std::vector<std::uint8_t> out(plaintext.size() + kAeadTagBytes);
    unsigned long long out_len = 0;
    crypto_aead_xchacha20poly1305_ietf_encrypt(
        out.data(), &out_len, plaintext.data(), plaintext.size(),
        associated_data.empty() ? nullptr : associated_data.data(), associated_data.size(),
        /*nsec=*/nullptr, nonce.data(), key.data());
    out.resize(static_cast<std::size_t>(out_len));
    return out;
}

std::optional<std::vector<std::uint8_t>> aead_decrypt(std::span<const std::uint8_t> key,
                                                       std::span<const std::uint8_t> nonce,
                                                       std::span<const std::uint8_t> ciphertext,
                                                       std::span<const std::uint8_t> associated_data) {
    ensure_initialized();
    if (key.size() != kAeadKeyBytes || nonce.size() != kAeadNonceBytes) {
        throw std::invalid_argument("aead_decrypt: wrong key or nonce length");
    }
    if (ciphertext.size() < kAeadTagBytes) {
        return std::nullopt;
    }

    std::vector<std::uint8_t> out(ciphertext.size() - kAeadTagBytes);
    unsigned long long out_len = 0;
    const int rc = crypto_aead_xchacha20poly1305_ietf_decrypt(
        out.data(), &out_len, /*nsec=*/nullptr, ciphertext.data(), ciphertext.size(),
        associated_data.empty() ? nullptr : associated_data.data(), associated_data.size(),
        nonce.data(), key.data());
    if (rc != 0) {
        return std::nullopt; // authentication failure: wrong key, wrong AD, or tampered bytes
    }
    out.resize(static_cast<std::size_t>(out_len));
    return out;
}

std::string generate_password(const PasswordPolicy& policy) {
    if (policy.length == 0) {
        return {};
    }

    std::string charset;
    if (policy.lowercase) {
        charset += "abcdefghijklmnopqrstuvwxyz";
    }
    if (policy.uppercase) {
        charset += "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    }
    if (policy.digits) {
        charset += "0123456789";
    }
    if (policy.symbols) {
        charset += "!@#$%^&*()-_=+[]{};:,.<>?/";
    }
    if (charset.empty()) {
        return {};
    }

    ensure_initialized();
    std::string out;
    out.reserve(policy.length);
    const auto charset_size = static_cast<std::uint32_t>(charset.size());
    for (std::size_t i = 0; i < policy.length; ++i) {
        // randombytes_uniform rejection-samples internally, so this is free
        // of modulo bias regardless of charset_size.
        out.push_back(charset[randombytes_uniform(charset_size)]);
    }
    return out;
}

bool constant_time_equal(std::span<const std::uint8_t> a, std::span<const std::uint8_t> b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    return sodium_memcmp(a.data(), b.data(), a.size()) == 0;
}

} // namespace nexus::crypto
