#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>

#include "nexus/module/backup/backup_engine.hpp" // Progress

namespace nexus::fs {
class ExclusionRules;
}

namespace nexus::module::backup {

struct SyncSummary {
    std::uint64_t files_copied = 0;
    std::uint64_t files_deleted = 0; ///< files removed from the destination
    std::uint64_t dirs_deleted = 0;  ///< directories removed wholesale
    std::uint64_t bytes_copied = 0;
    std::uint64_t errors = 0;
    bool cancelled = false;
};

/// One-way sync (Mirror mode, C1): makes `destination` match `source`
/// exactly - copies new/changed files, then deletes anything in the
/// destination that isn't in the source. Direct file copies, no content-
/// addressed store and no history, unlike BackupEngine/Snapshot mode.
class SyncEngine {
public:
    /// `throttle`, if given, is called once per file copied (UFR-017), same
    /// as BackupEngine::run().
    SyncSummary run(const std::filesystem::path& source, const std::filesystem::path& destination,
                    const nexus::fs::ExclusionRules& rules, const Progress& progress = {},
                    const std::function<bool()>& cancelled = {},
                    const std::function<void()>& throttle = {});
};

} // namespace nexus::module::backup
