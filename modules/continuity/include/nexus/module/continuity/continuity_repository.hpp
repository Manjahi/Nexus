#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "nexus/core/id.hpp"
#include "nexus/core/time.hpp"

namespace nexus::db {
class Database;
struct Migration;
}

namespace nexus::module::continuity {

[[nodiscard]] std::span<const nexus::db::Migration> continuity_migrations();

enum class AssetKind { File, Folder, App, Credential };
enum class Criticality { Low, Medium, High };

[[nodiscard]] std::string_view to_string(AssetKind kind) noexcept;
[[nodiscard]] std::optional<AssetKind> asset_kind_from_string(std::string_view text) noexcept;
[[nodiscard]] std::string_view to_string(Criticality criticality) noexcept;
[[nodiscard]] std::optional<Criticality> criticality_from_string(std::string_view text) noexcept;

/// Something the user has designated as "must survive a disaster". File/
/// Folder/App assets are identified by `path`; a Credential asset instead
/// points at a Vault entry by id (`vault_entry_id`) - it has no filesystem
/// presence to check.
struct TrackedAsset {
    nexus::core::Uuid id;
    std::string label;
    AssetKind kind = AssetKind::File;
    std::string path;
    std::string vault_entry_id;
    Criticality criticality = Criticality::Medium;
    nexus::core::Timestamp created_at{};
};

/// One run of "Run Quick Rehearsal" (or a named disaster scenario drill):
/// a real restore-to-scratch plus verify, not a simulated/estimated result.
struct RehearsalRecord {
    nexus::core::Uuid id;
    std::string scenario;
    nexus::core::Timestamp started_at{};
    std::optional<nexus::core::Timestamp> finished_at;
    std::string outcome; ///< "running" | "success" | "partial" | "failed"
    std::uint64_t files_restored = 0;
    std::uint64_t bytes_restored = 0;
    std::string detail;
};

/// Reads/writes the Continuity tables (continuity_assets,
/// continuity_rehearsals, continuity_capsule). Pure data access - readiness
/// scoring and scenario evaluation (which also read Backup/Vault state) live
/// in ContinuityModule, not here, matching how BackupRepository stays a
/// plain store while BackupModule owns the cross-module logic.
class ContinuityRepository {
public:
    explicit ContinuityRepository(nexus::db::Database& db) noexcept : db_(&db) {}

    /// Inserts a new asset (asset.id nil) or updates an existing one.
    nexus::core::Uuid upsert_asset(const TrackedAsset& asset);
    [[nodiscard]] std::optional<TrackedAsset> find_asset(const nexus::core::Uuid& id) const;
    [[nodiscard]] std::vector<TrackedAsset> list_assets() const;
    bool remove_asset(const nexus::core::Uuid& id);

    nexus::core::Uuid begin_rehearsal(std::string_view scenario);
    void finish_rehearsal(const nexus::core::Uuid& id, std::string_view outcome,
                          std::uint64_t files_restored, std::uint64_t bytes_restored,
                          std::string_view detail);
    [[nodiscard]] std::vector<RehearsalRecord> recent_rehearsals(std::size_t limit = 20) const;
    [[nodiscard]] std::optional<RehearsalRecord> latest_rehearsal() const;

    /// The Recovery Capsule's last successful export, recorded by the
    /// desktop layer (the only place with Vault access) right after it
    /// calls VaultStore::export_to() - read by the Computer Theft/New-PC
    /// Migration scenario checks and the Capsule card.
    void record_capsule_export(nexus::core::Timestamp at);
    [[nodiscard]] std::optional<nexus::core::Timestamp> latest_capsule_export() const;

private:
    nexus::db::Database* db_;
};

} // namespace nexus::module::continuity
