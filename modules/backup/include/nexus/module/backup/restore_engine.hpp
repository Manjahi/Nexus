#pragma once

#include "nexus/core/id.hpp"
#include "nexus/module/backup/backup_engine.hpp" // Progress

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace nexus::module::backup {

class ObjectStore;
class BackupRepository;

struct RestoreSummary {
    nexus::core::Uuid restore_id;
    std::uint64_t files_restored = 0;
    std::uint64_t missing_blobs = 0;
    std::uint64_t errors = 0;
    bool cancelled = false;

    [[nodiscard]] bool ok() const noexcept {
        return missing_blobs == 0 && errors == 0 && !cancelled;
    }
};

/// Recreates snapshot files from the ObjectStore into a target directory.
class RestoreEngine {
public:
    RestoreEngine(ObjectStore& store, BackupRepository& repo) noexcept
        : store_(&store), repo_(&repo) {}

    /// If `only_path` is non-empty, restores just that one file (matched by the
    /// stored relative path); otherwise restores the whole snapshot.
    RestoreSummary restore(const nexus::core::Uuid& snapshot_id,
                           const std::filesystem::path& target_dir, std::string_view only_path = {},
                           const Progress& progress = {},
                           const std::function<bool()>& cancelled = {});

private:
    ObjectStore* store_;
    BackupRepository* repo_;
};

} // namespace nexus::module::backup
