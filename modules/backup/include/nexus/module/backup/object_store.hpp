#pragma once

#include "nexus/hash/hash.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <unordered_set>

namespace nexus::module::backup {

/// Content-addressed blob store: each unique file content lives once at
/// <root>/<hex[0:2]>/<hex>, keyed by its BLAKE3 digest. This is what makes
/// snapshots incremental - only new content is written.
class ObjectStore {
public:
    explicit ObjectStore(std::filesystem::path root) : root_(std::move(root)) {}

    struct PutResult {
        nexus::hash::Digest digest{};
        std::uint64_t size = 0; ///< size of the source file
        bool was_new = false;   ///< false if the blob already existed
    };

    /// Hashes `source` and stores it if absent. nullopt on read error.
    [[nodiscard]] std::optional<PutResult> put_file(const std::filesystem::path& source);

    [[nodiscard]] bool contains(const nexus::hash::Digest& digest) const;

    /// Copies the blob to `destination` (creating parent directories). Returns
    /// false if the blob is missing or the copy fails.
    [[nodiscard]] bool extract_to(const nexus::hash::Digest& digest,
                                  const std::filesystem::path& destination) const;

    /// Re-hashes the stored blob and checks it against its name.
    [[nodiscard]] bool verify(const nexus::hash::Digest& digest) const;

    [[nodiscard]] std::filesystem::path path_for(const nexus::hash::Digest& digest) const;
    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }

    struct GcResult {
        std::uint64_t blobs_removed = 0;
        std::uint64_t bytes_reclaimed = 0;
    };

    /// Deletes every blob under root() whose hex digest isn't in `keep`
    /// (also sweeps stray temp files left by an interrupted put_file, since
    /// those never match a real digest either). Call after pruning old
    /// snapshots from the database - that only removes rows, not bytes.
    GcResult collect_garbage(const std::unordered_set<std::string>& keep) const;

private:
    std::filesystem::path root_;
};

} // namespace nexus::module::backup
