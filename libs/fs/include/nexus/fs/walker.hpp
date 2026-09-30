#pragma once

#include "nexus/fs/exclusion_rules.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>

namespace nexus::fs {

struct FileEntry {
    std::filesystem::path path;
    std::uint64_t size = 0;
    std::filesystem::file_time_type last_write_time{};
    bool is_symlink = false;
};

struct WalkOptions {
    bool follow_symlinks = false;
    int max_depth = -1; ///< -1 = unlimited; 0 = root's direct children only
};

struct WalkStats {
    std::uint64_t files = 0;
    std::uint64_t directories = 0;
    std::uint64_t bytes = 0;
    std::uint64_t excluded = 0;
    std::uint64_t errors = 0;
    bool cancelled = false;
};

using FileVisitor = std::function<void(const FileEntry&)>;

/// Recursively visits regular files under `root`, honouring `rules`. Excluded
/// directories are not descended into. Symlinked directories are skipped unless
/// `follow_symlinks`. Unreadable entries increment `errors` and are skipped.
/// `cancelled`, if provided, is polled between entries.
WalkStats walk(const std::filesystem::path& root, const ExclusionRules& rules,
               const WalkOptions& options, const FileVisitor& on_file,
               const std::function<bool()>& cancelled = {});

} // namespace nexus::fs
