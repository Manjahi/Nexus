#pragma once

#include "nexus/core/time.hpp"
#include "nexus/search/inverted_index.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nexus::fs {
class ExclusionRules;
}

namespace nexus::module::search {

class SearchRepository;

using IndexProgress = std::function<void(double, std::string_view)>;

struct IndexSummary {
    std::uint64_t files_seen = 0;
    std::uint64_t files_indexed = 0;
    std::uint64_t files_skipped = 0;
    std::uint64_t files_unchanged = 0; ///< same size+mtime as last index; not re-read
    std::uint64_t files_removed = 0;   ///< previously indexed under root, gone from disk now
    bool cancelled = false;
};

struct QueryResult {
    std::string path;
    double score = 0.0;
    std::size_t matched_terms = 0;
    std::string snippet;
};

/// Narrows query() results (C2). Both fields default to "no restriction" so
/// existing callers see identical behaviour when they don't pass one.
struct QueryFilter {
    /// Extension including the dot (e.g. ".txt"), matched case-insensitively.
    /// Empty means any extension.
    std::string extension;
    /// Only files modified at or after this time. Unset means any time.
    std::optional<nexus::core::Timestamp> modified_after;

    [[nodiscard]] bool active() const noexcept {
        return !extension.empty() || modified_after.has_value();
    }
};

/// Owns an in-memory BM25 index kept in sync with the search tables. Rebuilt
/// from persisted postings on construction.
class SearchIndexer {
public:
    explicit SearchIndexer(SearchRepository& repo);

    /// Walks `root` and (re)indexes indexable files whose size or mtime
    /// changed since they were last indexed (unchanged files are skipped
    /// without being re-read); also drops any file previously indexed
    /// under `root` that no longer exists on disk.
    IndexSummary index_tree(const std::filesystem::path& root,
                            const nexus::fs::ExclusionRules& rules,
                            const IndexProgress& progress = {},
                            const std::function<bool()>& cancelled = {});

    void remove_path(const std::filesystem::path& path);

    [[nodiscard]] std::vector<QueryResult> query(std::string_view text, std::size_t limit = 20,
                                                 const QueryFilter& filter = {}) const;

    [[nodiscard]] std::size_t indexed_documents() const noexcept { return index_.document_count(); }
    [[nodiscard]] std::size_t indexed_terms() const noexcept { return index_.term_count(); }

private:
    SearchRepository* repo_;
    nexus::search::InvertedIndex index_;
};

} // namespace nexus::module::search
