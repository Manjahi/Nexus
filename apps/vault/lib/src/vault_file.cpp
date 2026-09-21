#include "nexus/vault/vault_file.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <system_error>

namespace nexus::vault {

namespace {

constexpr std::array<char, 8> kMagic{'N', 'X', 'V', 'L', 'T', '0', '0', '1'};
constexpr std::uint32_t kFormatVersion = 1;

// The header's opslimit/memlimit are read from the file before the AEAD tag
// has been checked, so they're attacker-controlled on a corrupted or
// malicious vault file (the exact scenario the threat model calls out:
// restoring a vault from an untrusted backup). libsodium accepts opslimit up
// to ~4 billion and memlimit up to several TiB, and would spend unbounded
// time/memory honoring a claim in that range before crypto_pwhash ever gets
// a chance to fail - unlock() must never run the KDF against such a claim.
// Every vault this app writes today uses KdfParams::interactive() (opslimit=2,
// memlimit=64 MiB); these caps allow 2x that as tuning headroom without
// opening the door to a multi-second-or-worse KDF run from a hostile file.
// If a stronger opt-in profile is ever wired up (e.g. libsodium's "moderate":
// opslimit=3, memlimit=256 MiB), raise these caps to match it then.
constexpr std::uint64_t kMaxAcceptedOpslimit = 4;
constexpr std::uint64_t kMaxAcceptedMemlimit = 128ULL * 1024 * 1024; // 128 MiB

void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF));
    }
}

void append_u64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF));
    }
}

std::optional<std::uint32_t> read_u32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset + 4 > bytes.size()) {
        return std::nullopt;
    }
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        value |= static_cast<std::uint32_t>(bytes[offset + static_cast<std::size_t>(i)]) << (8 * i);
    }
    return value;
}

std::optional<std::uint64_t> read_u64(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset + 8 > bytes.size()) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(bytes[offset + static_cast<std::size_t>(i)]) << (8 * i);
    }
    return value;
}

/// Builds the fixed prefix: magic + format_version + opslimit + memlimit +
/// salt_len + salt. This is also the AEAD associated data, binding the
/// ciphertext to the exact KDF parameters and salt it was sealed under.
std::vector<std::uint8_t> build_header_bytes(const VaultHeader& header) {
    std::vector<std::uint8_t> out;
    out.insert(out.end(), kMagic.begin(), kMagic.end());
    append_u32(out, header.format_version);
    append_u64(out, header.kdf_params.opslimit);
    append_u64(out, static_cast<std::uint64_t>(header.kdf_params.memlimit));
    append_u32(out, static_cast<std::uint32_t>(header.salt.size()));
    out.insert(out.end(), header.salt.begin(), header.salt.end());
    return out;
}

struct ParsedFile {
    VaultHeader header;
    std::size_t header_len = 0; // bytes consumed by the header (for AD/nonce offset)
    std::vector<std::uint8_t> nonce;
    std::vector<std::uint8_t> ciphertext;
};

std::optional<ParsedFile> parse_file(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < kMagic.size() ||
        !std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
        return std::nullopt;
    }

    std::size_t offset = kMagic.size();
    const auto version = read_u32(bytes, offset);
    if (!version || *version != kFormatVersion) {
        return std::nullopt; // unknown format version - nothing to fall back to yet
    }
    offset += 4;

    const auto opslimit = read_u64(bytes, offset);
    if (!opslimit || *opslimit > kMaxAcceptedOpslimit) {
        // Upper-bound check before this value ever reaches derive_key()/
        // crypto_pwhash() - see kMaxAcceptedOpslimit's comment above. No
        // lower bound: a too-small value only makes the KDF weaker (and
        // legitimate low-cost profiles, e.g. tests, use one), never slower -
        // it's not part of the DoS surface this guards against.
        return std::nullopt;
    }
    offset += 8;

    const auto memlimit = read_u64(bytes, offset);
    if (!memlimit || *memlimit > kMaxAcceptedMemlimit) {
        return std::nullopt;
    }
    offset += 8;

    const auto salt_len = read_u32(bytes, offset);
    if (!salt_len || *salt_len != nexus::crypto::kKdfSaltBytes) {
        return std::nullopt;
    }
    offset += 4;

    if (offset + *salt_len > bytes.size()) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> salt(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                                   bytes.begin() + static_cast<std::ptrdiff_t>(offset + *salt_len));
    offset += *salt_len;

    ParsedFile parsed;
    parsed.header.format_version = *version;
    parsed.header.kdf_params.opslimit = *opslimit;
    parsed.header.kdf_params.memlimit = static_cast<std::size_t>(*memlimit);
    parsed.header.salt = std::move(salt);
    parsed.header_len = offset;

    if (offset + nexus::crypto::kAeadNonceBytes > bytes.size()) {
        return std::nullopt;
    }
    parsed.nonce.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                        bytes.begin() +
                            static_cast<std::ptrdiff_t>(offset + nexus::crypto::kAeadNonceBytes));
    offset += nexus::crypto::kAeadNonceBytes;

    parsed.ciphertext.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset), bytes.end());
    return parsed;
}

std::optional<std::vector<std::uint8_t>> read_file_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        return std::nullopt;
    }
    const std::streamsize size = in.tellg();
    if (size < 0) {
        return std::nullopt;
    }
    in.seekg(0);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty() && !in.read(reinterpret_cast<char*>(bytes.data()), size)) {
        return std::nullopt;
    }
    return bytes;
}

} // namespace

bool VaultFile::exists(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

std::optional<VaultHeader> VaultFile::read_header(const std::filesystem::path& path) {
    const auto bytes = read_file_bytes(path);
    if (!bytes) {
        return std::nullopt;
    }
    const auto parsed = parse_file(*bytes);
    if (!parsed) {
        return std::nullopt;
    }
    return parsed->header;
}

std::optional<UnlockedVault> VaultFile::create(const std::filesystem::path& path,
                                               std::string_view master_password,
                                               const nexus::crypto::KdfParams& params) {
    if (exists(path)) {
        return std::nullopt;
    }

    VaultHeader header;
    header.format_version = kFormatVersion;
    header.kdf_params = params;
    header.salt = nexus::crypto::random_bytes(nexus::crypto::kKdfSaltBytes);

    auto key = nexus::crypto::derive_key(master_password, header.salt, nexus::crypto::kAeadKeyBytes,
                                         params);
    if (!key) {
        return std::nullopt;
    }

    if (!save(path, header, key->span(), {})) {
        return std::nullopt;
    }

    return UnlockedVault{std::move(header), std::move(*key), {}};
}

std::optional<UnlockedVault> VaultFile::unlock(const std::filesystem::path& path,
                                               std::string_view master_password) {
    const auto bytes = read_file_bytes(path);
    if (!bytes) {
        return std::nullopt;
    }
    const auto parsed = parse_file(*bytes);
    if (!parsed) {
        return std::nullopt;
    }

    auto key = nexus::crypto::derive_key(master_password, parsed->header.salt,
                                         nexus::crypto::kAeadKeyBytes, parsed->header.kdf_params);
    if (!key) {
        return std::nullopt;
    }

    const auto header_bytes = build_header_bytes(parsed->header);
    const auto plaintext =
        nexus::crypto::aead_decrypt(key->span(), parsed->nonce, parsed->ciphertext, header_bytes);
    if (!plaintext) {
        return std::nullopt; // wrong password or corrupted/tampered file - indistinguishable
    }

    auto entries = parse_entries(
        std::string_view(reinterpret_cast<const char*>(plaintext->data()), plaintext->size()));
    if (!entries) {
        return std::nullopt;
    }

    return UnlockedVault{parsed->header, std::move(*key), std::move(*entries)};
}

bool VaultFile::save(const std::filesystem::path& path, const VaultHeader& header,
                     std::span<const std::uint8_t> key, const std::vector<Entry>& entries) {
    const auto header_bytes = build_header_bytes(header);
    const auto nonce = nexus::crypto::random_bytes(nexus::crypto::kAeadNonceBytes);

    const std::string plaintext = serialize_entries(entries);
    const auto plaintext_bytes =
        std::as_bytes(std::span(plaintext.data(), plaintext.size()));
    std::vector<std::uint8_t> plaintext_u8(plaintext_bytes.size());
    std::memcpy(plaintext_u8.data(), plaintext_bytes.data(), plaintext_bytes.size());

    const auto ciphertext = nexus::crypto::aead_encrypt(key, nonce, plaintext_u8, header_bytes);

    const std::filesystem::path temp_path = path.string() + ".tmp";
    {
        std::ofstream out(temp_path, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        out.write(reinterpret_cast<const char*>(header_bytes.data()),
                  static_cast<std::streamsize>(header_bytes.size()));
        out.write(reinterpret_cast<const char*>(nonce.data()),
                  static_cast<std::streamsize>(nonce.size()));
        out.write(reinterpret_cast<const char*>(ciphertext.data()),
                  static_cast<std::streamsize>(ciphertext.size()));
        if (!out) {
            return false;
        }
    }

    std::error_code ec;
    std::filesystem::rename(temp_path, path, ec);
    if (ec) {
        std::filesystem::remove(temp_path, ec);
        return false;
    }
    return true;
}

} // namespace nexus::vault
