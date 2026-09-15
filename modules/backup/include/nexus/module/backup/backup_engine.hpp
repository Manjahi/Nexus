#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string_view>

#include "nexus/core/id.hpp"

namespace nexus::fs {
class ExclusionRules;
}

namespace nexus::module::backup {

class ObjectStore;
class BackupRepository;

using Progress = std::function<void(double, std::string_view)>;

struct SnapshotSummary {
    nexus::core::Uuid snapshot_id;
    std::uint64_t file_count = 0;
    std::uint64_t total_bytes = 0;
    std::uint64_t new_bytes = 0; ///< bytes written to the store this run (dedup savings = total - new)
    std::uint64_t errors = 0;
    bool cancelled = false;
};

struct VerifyResult {
    std::uint64_t checked = 0;
    std::uint64_t ok = 0;
    std::uint64_t corrupt = 0;
    std::uint64_t missing = 0;

    [[nodiscard]] bool healthy() const noexcept { return corrupt == 0 && missing == 0; }
};

/// Walks a source tree, writes new file contents into the ObjectStore, and
/// records a snapshot. Incremental behaviour comes from the store dedup.
class BackupEngine {
public:
    BackupEngine(ObjectStore& store, BackupRepository* repo = nullptr) noexcept
        : store_(&store), repo_(repo) {}

    /// `throttle` (UFR-017), if given, is called once per file backed up - a
    /// no-op at Unlimited, a brief sleep otherwise (see nexus::jobs::Throttle).
    SnapshotSummary run(const nexus::core::Uuid& job_id, const std::filesystem::path& source,
                        const nexus::fs::ExclusionRules& rules, const Progress& progress = {},
                        const std::function<bool()>& cancelled = {},
                        const std::function<void()>& throttle = {});

    /// Re-hashes every blob referenced by a snapshot.
    [[nodiscard]] VerifyResult verify(const nexus::core::Uuid& snapshot_id,
                                      const std::function<bool()>& cancelled = {}) const;

private:
    ObjectStore* store_;
    BackupRepository* repo_;
};

} // namespace nexus::module::backup
