#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace nexus::module::storage {

struct RecycleResult {
    std::size_t requested = 0;
    std::size_t recycled = 0;
    std::vector<std::string> failed; ///< paths that could not be moved
    std::string error;               ///< set if the whole operation could not start

    [[nodiscard]] bool ok() const noexcept { return error.empty() && failed.empty(); }
};

/// Moves each path to the OS Recycle Bin (reversible; UFR-013). Missing paths
/// count as failures. On non-Windows this is a no-op that reports every path as
/// failed.
[[nodiscard]] RecycleResult recycle_to_bin(std::span<const std::filesystem::path> paths);

} // namespace nexus::module::storage
