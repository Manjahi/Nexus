#include "nexus/hash/hash.hpp"

#include <blake3.h>
#include <sodium.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <vector>

namespace nexus::hash {

namespace {

constexpr std::size_t kFileBufferSize = 1 << 20; // 1 MiB

int hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower >= 'a' && lower <= 'f') {
        return 10 + (lower - 'a');
    }
    return -1;
}

} // namespace

std::string_view to_string(Algorithm algorithm) noexcept {
    return algorithm == Algorithm::Sha256 ? "sha256" : "blake3";
}

struct Hasher::State {
    blake3_hasher blake3{};
    crypto_hash_sha256_state sha256{};
};

Hasher::Hasher(Algorithm algorithm)
    : algorithm_(algorithm), state_(std::make_unique<State>()) {
    if (algorithm_ == Algorithm::Sha256) {
        crypto_hash_sha256_init(&state_->sha256);
    } else {
        blake3_hasher_init(&state_->blake3);
    }
}

Hasher::~Hasher() = default;
Hasher::Hasher(Hasher&&) noexcept = default;
Hasher& Hasher::operator=(Hasher&&) noexcept = default;

void Hasher::update(std::span<const std::byte> data) {
    if (data.empty()) {
        return;
    }
    if (algorithm_ == Algorithm::Sha256) {
        crypto_hash_sha256_update(&state_->sha256,
                                  reinterpret_cast<const unsigned char*>(data.data()), data.size());
    } else {
        blake3_hasher_update(&state_->blake3, data.data(), data.size());
    }
}

Digest Hasher::finish() {
    Digest out{};
    if (algorithm_ == Algorithm::Sha256) {
        crypto_hash_sha256_final(&state_->sha256, out.data());
    } else {
        blake3_hasher_finalize(&state_->blake3, out.data(), out.size());
    }
    return out;
}

std::string to_hex(const Digest& digest) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(digest.size() * 2);
    for (const std::uint8_t byte : digest) {
        out.push_back(digits[(byte >> 4) & 0x0F]);
        out.push_back(digits[byte & 0x0F]);
    }
    return out;
}

std::optional<Digest> digest_from_hex(std::string_view text) {
    if (text.size() != 64) {
        return std::nullopt;
    }
    Digest out{};
    for (std::size_t i = 0; i < out.size(); ++i) {
        const int hi = hex_value(text[2 * i]);
        const int lo = hex_value(text[2 * i + 1]);
        if (hi < 0 || lo < 0) {
            return std::nullopt;
        }
        out[i] = static_cast<std::uint8_t>((hi << 4) | lo);
    }
    return out;
}

Digest hash_bytes(std::span<const std::byte> data, Algorithm algorithm) {
    Hasher hasher(algorithm);
    hasher.update(data);
    return hasher.finish();
}

namespace {

std::optional<Digest> hash_stream(const std::filesystem::path& path, Algorithm algorithm,
                                  std::uint64_t max_bytes) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }

    Hasher hasher(algorithm);
    std::vector<char> buffer(kFileBufferSize);
    std::uint64_t remaining = max_bytes;

    while (remaining > 0) {
        const std::streamsize want = static_cast<std::streamsize>(
            std::min<std::uint64_t>(remaining, buffer.size()));
        in.read(buffer.data(), want);
        const std::streamsize got = in.gcount();
        if (got > 0) {
            hasher.update(std::as_bytes(std::span(buffer.data(), static_cast<std::size_t>(got))));
            remaining -= static_cast<std::uint64_t>(got);
        }
        if (got < want) {
            break; // EOF
        }
    }

    if (in.bad()) {
        return std::nullopt;
    }
    return hasher.finish();
}

} // namespace

std::optional<Digest> hash_file(const std::filesystem::path& path, Algorithm algorithm) {
    return hash_stream(path, algorithm, UINT64_MAX);
}

std::optional<Digest> hash_file_prefix(const std::filesystem::path& path, std::uint64_t max_bytes,
                                       Algorithm algorithm) {
    return hash_stream(path, algorithm, max_bytes);
}

} // namespace nexus::hash
