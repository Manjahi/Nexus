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
#include "nexus/module/storage/duplicate_scanner.hpp"

namespace nexus::db {
class Database;
struct Migration;
}

namespace nexus::module::storage {

[[nodiscard]] std::span<const nexus::db::Migration> storage_migrations();

struct ScanRecord {
    nexus::core::Uuid id;
    std::string root;
    nexus::core::Timestamp started_at{};
    std::optional<nexus::core::Timestamp> finished_at;
    std::uint64_t files_seen = 0;
    std::uint64_t bytes_seen = 0;
    int duplicate_groups = 0;
    std::uint64_t reclaimable_bytes = 0;
    std::string state;
};

struct GroupRecord {
    std::int64_t id = 0;
    std::string digest;
    std::uint64_t file_size = 0;
    int file_count = 0;
    std::uint64_t reclaimable_bytes = 0;
};

/// Reads/writes the Storage tables (file_scans, duplicate_groups, scanned_files).
class StorageRepository {
public:
    explicit StorageRepository(nexus::db::Database& db) noexcept : db_(&db) {}

    [[nodiscard]] nexus::core::Uuid begin_scan(std::string_view root);
    void finish_scan(const nexus::core::Uuid& scan_id, const ScanSummary& summary,
                     std::string_view state = "completed");

    /// Inserts a group and its files (with group_id set). Returns the group id.
    std::int64_t add_group(const nexus::core::Uuid& scan_id, const DuplicateGroup& group);

    [[nodiscard]] std::vector<ScanRecord> scans(std::size_t limit = 20) const;
    [[nodiscard]] std::optional<ScanRecord> latest_scan() const;
    [[nodiscard]] std::vector<GroupRecord> groups_for(const nexus::core::Uuid& scan_id) const;
    [[nodiscard]] std::vector<std::string> files_in_group(std::int64_t group_id) const;

    std::int64_t prune_scans_keeping(std::size_t keep);

private:
    nexus::db::Database* db_;
};

} // namespace nexus::module::storage
