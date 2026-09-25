#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "nexus/core/id.hpp"

namespace nexus::db {
class Database;
}

namespace nexus::module::continuity {

struct RehearsalResult {
    nexus::core::Uuid rehearsal_id;
    std::string outcome; ///< "success" | "partial" | "failed"
    std::uint64_t files_restored = 0;
    std::uint64_t bytes_restored = 0;
    std::string detail;
};

/// Runs a real disaster-recovery drill, not a simulated one: restores the
/// most-recently-touched backup job's latest snapshot into `scratch_dir`
/// (caller-owned - created or removed by neither this function nor its
/// caller's responsibility to decide, so tests can inspect what landed
/// there) via the same RestoreEngine the Backup page's own restore button
/// uses, then verifies that snapshot's blobs via BackupEngine::verify() -
/// marking it verified in BackupRepository on a clean result, same as a
/// manual Verify would. The outcome is recorded via
/// ContinuityRepository::begin_rehearsal()/finish_rehearsal() before this
/// returns.
///
/// Outcome is "failed" when there is no backup snapshot to rehearse at all;
/// "partial" when some files or blobs came back missing/corrupt; "success"
/// otherwise.
[[nodiscard]] RehearsalResult run_quick_rehearsal(nexus::db::Database& db,
                                                  const std::filesystem::path& scratch_dir);

} // namespace nexus::module::continuity
