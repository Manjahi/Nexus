#pragma once

#include "nexus/search/tokenizer.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace nexus::search {

using DocId = std::uint64_t;

struct SearchHit {
    DocId id = 0;
    double score = 0.0;
    std::size_t matched_terms = 0;
};

/// In-memory inverted index with Okapi BM25 ranking. Not thread-safe.
class InvertedIndex {
public:
    /// Adds or replaces a document.
    void add_document(DocId id, std::string_view text, const TokenizeOptions& options = {});

    /// Adds or replaces a document from pre-computed term frequencies (for
    /// loading a persisted index without re-reading files).
    void add_document_postings(DocId id, const std::map<std::string, std::uint32_t>& frequencies);

    bool remove_document(DocId id);
    [[nodiscard]] bool contains(DocId id) const;

    /// Ranked results, best first. Documents matching more distinct query terms
    /// score higher (BM25 contributions sum).
    [[nodiscard]] std::vector<SearchHit> search(std::string_view query, std::size_t limit = 20,
                                                const TokenizeOptions& options = {}) const;

    [[nodiscard]] std::size_t document_count() const noexcept { return doc_lengths_.size(); }
    [[nodiscard]] std::size_t term_count() const noexcept { return postings_.size(); }

    // BM25 parameters.
    double k1 = 1.2;
    double b = 0.75;

private:
    void install(DocId id, const std::map<std::string, std::uint32_t>& frequencies);

    struct Posting {
        DocId doc;
        std::uint32_t term_frequency;
    };

    std::unordered_map<std::string, std::vector<Posting>> postings_;
    std::unordered_map<DocId, std::uint32_t> doc_lengths_;
    std::uint64_t total_length_ = 0;
};

} // namespace nexus::search
