#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "nexus/crypto/crypto.hpp"
#include "nexus/vault/entry.hpp"

namespace nexus::vault {

/// The unencrypted portion of a vault file: enough to derive the key, but
/// nothing that reveals vault contents.
struct VaultHeader {
    std::uint32_t format_version = 1;
    nexus::crypto::KdfParams kdf_params;
    std::vector<std::uint8_t> salt; ///< kKdfSaltBytes long
};

/// The result of a successful create() or unlock(): the header (for the next
/// save), the derived key (kept only in secure memory - the KDF runs once
/// per unlock, never again per-save), and the decrypted entries.
struct UnlockedVault {
    VaultHeader header;
    nexus::crypto::SecureBuffer key;
    std::vector<Entry> entries;
};

/// Reads/writes the on-disk vault format (see ADR-0003 and the threat model
/// doc under docs/security/). Every function here is a pure file operation -
/// no in-memory session state; VaultStore builds on top of this to hold an
/// unlocked session.
class VaultFile {
public:
    VaultFile() = delete;

    [[nodiscard]] static bool exists(const std::filesystem::path& path);

    /// Reads just the header (fast, no password needed) - enough to know a
    /// path holds a vault before asking the user to unlock it.
    [[nodiscard]] static std::optional<VaultHeader> read_header(const std::filesystem::path& path);

    /// Creates a brand-new, empty vault at `path`. Fails if a file already
    /// exists there. Runs the KDF once and returns the derived key so the
    /// caller doesn't pay for a second derivation on the first save.
    [[nodiscard]] static std::optional<UnlockedVault> create(
        const std::filesystem::path& path, std::string_view master_password,
        const nexus::crypto::KdfParams& params = nexus::crypto::KdfParams::interactive());

    /// Decrypts the vault at `path` with `master_password`. nullopt covers a
    /// missing/corrupt file and a wrong password alike - the two are
    /// indistinguishable by design (an AEAD auth failure doesn't say why).
    [[nodiscard]] static std::optional<UnlockedVault> unlock(const std::filesystem::path& path,
                                                             std::string_view master_password);

    /// Re-encrypts `entries` under the already-derived `key` (no KDF run - it
    /// only ever runs in create()/unlock()) with a fresh random nonce, and
    /// atomically replaces `path` (write to a temp file, then rename).
    [[nodiscard]] static bool save(const std::filesystem::path& path, const VaultHeader& header,
                                   std::span<const std::uint8_t> key,
                                   const std::vector<Entry>& entries);
};

} // namespace nexus::vault
