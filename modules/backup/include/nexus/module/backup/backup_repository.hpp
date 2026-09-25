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

namespace nexus::module::backup {

[[nodiscard]] std::span<const nexus::db::Migration> backup_migrations();

struct BackupJob {
    nexus::core::Uuid id;
    std::string name;
    std::string source_root;
    std::string destination;
    std::string schedule;
    int retention_keep = 10;
    bool enabled = true;
    std::string exclusions; ///< newline-separated ExclusionRules text
    nexus::core::Timestamp created_at{};
};

struct SnapshotRecord {
    nexus::core::Uuid id;
    nexus::core::Uuid backup_job_id;
    nexus::core::Timestamp started_at{};
    std::optional<nexus::core::Timestamp> finished_at;
    std::string state;
    std::uint64_t file_count = 0;
    std::uint64_t total_bytes = 0;
    std::uint64_t new_bytes = 0;
    /// Set by mark_verified() - the last time BackupEngine::verify() found
    /// this snapshot fully intact (no corrupt/missing objects). Unset means
    /// "never verified", not "verify failed" (a failed verify doesn't call
    /// mark_verified() at all, so it stays unset until a later success).
    std::optional<nexus::core::Timestamp> verified_at;
};

struct SnapshotFile {
    std::string path;
    std::uint64_t size = 0;
    std::string mtime;
    std::string digest; ///< BLAKE3 hex
};

class BackupRepository {
public:
    explicit BackupRepository(nexus::db::Database& db) noexcept : db_(&db) {}

    nexus::core::Uuid upsert_job(const BackupJob& job);
    [[nodiscard]] std::optional<BackupJob> find_job(const nexus::core::Uuid& id) const;
    [[nodiscard]] std::vector<BackupJob> list_jobs() const;
    bool remove_job(const nexus::core::Uuid& id);

    nexus::core::Uuid begin_snapshot(const nexus::core::Uuid& job_id);
    void add_snapshot_files(const nexus::core::Uuid& snapshot_id,
                            std::span<const SnapshotFile> files);
    void finish_snapshot(const nexus::core::Uuid& snapshot_id, std::string_view state,
                         std::uint64_t file_count, std::uint64_t total_bytes,
                         std::uint64_t new_bytes);
    void mark_verified(const nexus::core::Uuid& snapshot_id, nexus::core::Timestamp at);

    [[nodiscard]] std::vector<SnapshotRecord> snapshots_for(const nexus::core::Uuid& job_id,
                                                            std::size_t limit = 50) const;
    [[nodiscard]] std::optional<SnapshotRecord> latest_snapshot(const nexus::core::Uuid& job_id) const;
    [[nodiscard]] std::vector<SnapshotFile> files_in(const nexus::core::Uuid& snapshot_id) const;

    /// Deletes all but the newest `keep` completed snapshots of a job. Returns
    /// the ids removed (blobs are not GC'd here - see ObjectStore::collect_garbage,
    /// meant to be called with the result of all_referenced_digests() after this).
    std::vector<nexus::core::Uuid> prune_snapshots(const nexus::core::Uuid& job_id,
                                                   std::size_t keep);

    /// Distinct content digests (hex) still referenced by any snapshot_files
    /// row, across every job. A conservative (superset) keep-set for
    /// ObjectStore::collect_garbage - global rather than per-job so two jobs
    /// that happen to share a destination never lose a blob one of them
    /// still needs.
    [[nodiscard]] std::vector<std::string> all_referenced_digests() const;

    nexus::core::Uuid record_restore(const nexus::core::Uuid& snapshot_id,
                                     std::string_view target_dir, std::string_view state,
                                     std::uint64_t files_restored);

private:
    nexus::db::Database* db_;
};

} // namespace nexus::module::backup
