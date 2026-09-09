#include "nexus/search/inverted_index.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace nexus::search {

void InvertedIndex::add_document(DocId id, std::string_view text, const TokenizeOptions& options) {
    remove_document(id);

    std::map<std::string, std::uint32_t> frequencies;
    std::uint32_t length = 0;
    for (const Token& token : tokenize(text, options)) {
        ++frequencies[token.term];
        ++length;
    }
    if (length == 0) {
        return;
    }

    for (const auto& [term, frequency] : frequencies) {
        postings_[term].push_back({id, frequency});
    }
    doc_lengths_[id] = length;
    total_length_ += length;
}

bool InvertedIndex::remove_document(DocId id) {
    const auto it = doc_lengths_.find(id);
    if (it == doc_lengths_.end()) {
        return false;
    }
    total_length_ -= it->second;
    doc_lengths_.erase(it);

    for (auto term_it = postings_.begin(); term_it != postings_.end();) {
        std::vector<Posting>& list = term_it->second;
        std::erase_if(list, [&](const Posting& p) { return p.doc == id; });
        if (list.empty()) {
            term_it = postings_.erase(term_it);
        } else {
            ++term_it;
        }
    }
    return true;
}

bool InvertedIndex::contains(DocId id) const {
    return doc_lengths_.find(id) != doc_lengths_.end();
}

std::vector<SearchHit> InvertedIndex::search(std::string_view query, std::size_t limit,
                                             const TokenizeOptions& options) const {
    const std::size_t n = doc_lengths_.size();
    if (n == 0) {
        return {};
    }
    const double avg_length = static_cast<double>(total_length_) / static_cast<double>(n);

    // Distinct query terms.
    std::vector<std::string> terms = tokenize_terms(query, options);
    std::sort(terms.begin(), terms.end());
    terms.erase(std::unique(terms.begin(), terms.end()), terms.end());

    std::unordered_map<DocId, double> scores;
    std::unordered_map<DocId, std::size_t> matches;

    for (const std::string& term : terms) {
        const auto it = postings_.find(term);
        if (it == postings_.end()) {
            continue;
        }
        const std::vector<Posting>& list = it->second;
        const double df = static_cast<double>(list.size());
        const double idf =
            std::log(1.0 + (static_cast<double>(n) - df + 0.5) / (df + 0.5));

        for (const Posting& posting : list) {
            const auto len_it = doc_lengths_.find(posting.doc);
            if (len_it == doc_lengths_.end()) {
                continue;
            }
            const double tf = static_cast<double>(posting.term_frequency);
            const double doc_len = static_cast<double>(len_it->second);
            const double denom = tf + k1 * (1.0 - b + b * doc_len / avg_length);
            scores[posting.doc] += idf * (tf * (k1 + 1.0)) / denom;
            ++matches[posting.doc];
        }
    }

    std::vector<SearchHit> hits;
    hits.reserve(scores.size());
    for (const auto& [doc, score] : scores) {
        hits.push_back({doc, score, matches[doc]});
    }
    std::sort(hits.begin(), hits.end(), [](const SearchHit& a, const SearchHit& b) {
        if (a.matched_terms != b.matched_terms) {
            return a.matched_terms > b.matched_terms;
        }
        if (a.score != b.score) {
            return a.score > b.score;
        }
        return a.id < b.id;
    });
    if (hits.size() > limit) {
        hits.resize(limit);
    }
    return hits;
}

} // namespace nexus::search
