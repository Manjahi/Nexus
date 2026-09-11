#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "nexus/crypto/crypto.hpp"
#include "nexus/vault/vault_file.hpp"

namespace nexus::vault {

/// Holds one unlock session over a vault file: derives the key once (in
/// create()/unlock()), keeps decrypted entries and the key in memory only
/// while unlocked, and re-seals the whole entry list to disk on every
/// mutation. This is the class nexuspc-vault's IPC handlers call directly;
/// nothing outside apps/vault ever touches a VaultStore or an UnlockedVault.
class VaultStore {
public:
    explicit VaultStore(std::filesystem::path path) noexcept : path_(std::move(path)) {}

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
    [[nodiscard]] bool vault_exists() const { return VaultFile::exists(path_); }
    [[nodiscard]] bool locked() const noexcept { return !unlocked_.has_value(); }
    [[nodiscard]] std::size_t entry_count() const noexcept {
        return unlocked_ ? unlocked_->entries.size() : 0;
    }

    /// Creates a brand-new vault on disk and leaves this store unlocked over
    /// it. False if a vault already exists at path() or creation failed.
    [[nodiscard]] bool create(
        std::string_view master_password,
        const nexus::crypto::KdfParams& params = nexus::crypto::KdfParams::interactive());

    [[nodiscard]] bool unlock(std::string_view master_password);

    /// Zeroes the derived key and every entry's secret fields, then drops
    /// them. Independent of anything else in the process - callers (the IPC
    /// server's auto-lock timer, an explicit lock request) just call this.
    void lock() noexcept;

    [[nodiscard]] std::vector<EntrySummary> list() const;
    [[nodiscard]] std::optional<Entry> get(std::string_view id) const;

    /// Creates (entry.id empty) or updates (entry.id set and must already
    /// exist) an entry and persists the whole vault. Returns the entry's id
    /// on success; nullopt if locked, the id doesn't exist (update), or the
    /// save failed.
    [[nodiscard]] std::optional<std::string> put(Entry entry);

    /// Removes an entry and persists. False if locked or the id doesn't exist.
    bool remove(std::string_view id);

    [[nodiscard]] std::vector<HealthFinding> health() const;

private:
    [[nodiscard]] bool persist();

    std::filesystem::path path_;
    std::optional<UnlockedVault> unlocked_;
};

} // namespace nexus::vault
