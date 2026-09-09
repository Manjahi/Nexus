#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "nexus/search/inverted_index.hpp"

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
    bool cancelled = false;
};

struct QueryResult {
    std::string path;
    double score = 0.0;
    std::size_t matched_terms = 0;
    std::string snippet;
};

/// Owns an in-memory BM25 index kept in sync with the search tables. Rebuilt
/// from persisted postings on construction.
class SearchIndexer {
public:
    explicit SearchIndexer(SearchRepository& repo);

    /// Walks `root`, extracts text from indexable files, and (re)indexes each.
    IndexSummary index_tree(const std::filesystem::path& root,
                            const nexus::fs::ExclusionRules& rules,
                            const IndexProgress& progress = {},
                            const std::function<bool()>& cancelled = {});

    void remove_path(const std::filesystem::path& path);

    [[nodiscard]] std::vector<QueryResult> query(std::string_view text, std::size_t limit = 20) const;

    [[nodiscard]] std::size_t indexed_documents() const noexcept {
        return index_.document_count();
    }
    [[nodiscard]] std::size_t indexed_terms() const noexcept { return index_.term_count(); }

private:
    SearchRepository* repo_;
    nexus::search::InvertedIndex index_;
};

} // namespace nexus::module::search
