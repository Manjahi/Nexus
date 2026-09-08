#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "nexus/core/id.hpp"

namespace nexus::fs {
class ExclusionRules;
}

namespace nexus::module::storage {

class StorageRepository;

struct ScanOptions {
    std::uint64_t min_file_size = 1;
    std::uint64_t partial_hash_bytes = 16 * 1024;
    bool persist = true;
};

struct DuplicateGroup {
    std::string digest; ///< BLAKE3 hex of the full file content
    std::uint64_t file_size = 0;
    std::vector<std::filesystem::path> files; ///< at least two

    [[nodiscard]] std::uint64_t reclaimable_bytes() const noexcept {
        return files.size() < 2 ? 0 : file_size * (static_cast<std::uint64_t>(files.size()) - 1);
    }
};

struct ScanSummary {
    nexus::core::Uuid scan_id;
    std::uint64_t files_seen = 0;
    std::uint64_t bytes_seen = 0;
    std::uint64_t files_hashed = 0;
    std::vector<DuplicateGroup> groups;
    bool cancelled = false;

    [[nodiscard]] std::uint64_t reclaimable_bytes() const noexcept;
    [[nodiscard]] std::uint64_t duplicate_file_count() const noexcept;
};

/// fraction in [0, 1], plus a short phase label ("walking", "hashing", ...).
using ScanProgress = std::function<void(double, std::string_view)>;

/// Size-group -> partial hash -> full hash duplicate detection.
class DuplicateScanner {
public:
    /// `repo` may be null to skip persistence (tests / dry runs).
    explicit DuplicateScanner(StorageRepository* repo = nullptr) noexcept : repo_(repo) {}

    ScanSummary scan(const std::filesystem::path& root, const nexus::fs::ExclusionRules& rules,
                     const ScanOptions& options = {}, const ScanProgress& progress = {},
                     const std::function<bool()>& cancelled = {});

private:
    StorageRepository* repo_;
};

} // namespace nexus::module::storage
