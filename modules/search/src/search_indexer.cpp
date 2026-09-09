#include "nexus/module/search/search_indexer.hpp"

#include <algorithm>
#include <chrono>
#include <map>
#include <string>

#include "nexus/fs/exclusion_rules.hpp"
#include "nexus/fs/walker.hpp"
#include "nexus/module/search/content_reader.hpp"
#include "nexus/module/search/search_repository.hpp"
#include "nexus/search/snippet.hpp"
#include "nexus/search/tokenizer.hpp"

namespace nexus::module::search {

namespace fs = std::filesystem;

namespace {

void emit(const IndexProgress& progress, double fraction, std::string_view phase) {
    if (progress) {
        progress(std::clamp(fraction, 0.0, 1.0), phase);
    }
}

std::map<std::string, std::uint32_t> term_frequencies(std::string_view text) {
    std::map<std::string, std::uint32_t> freq;
    for (const auto& token : nexus::search::tokenize(text, {})) {
        ++freq[token.term];
    }
    return freq;
}

} // namespace

SearchIndexer::SearchIndexer(SearchRepository& repo) : repo_(&repo) {
    std::map<std::int64_t, std::map<std::string, std::uint32_t>> by_doc;
    for (const StoredPosting& posting : repo_->all_postings()) {
        by_doc[posting.doc_id][posting.term] = posting.tf;
    }
    for (const auto& [doc_id, freqs] : by_doc) {
        index_.add_document_postings(static_cast<nexus::search::DocId>(doc_id), freqs);
    }
}

IndexSummary SearchIndexer::index_tree(const fs::path& root, const nexus::fs::ExclusionRules& rules,
                                       const IndexProgress& progress,
                                       const std::function<bool()>& cancelled) {
    IndexSummary summary;
    const auto job_id = repo_->begin_index_job(root.generic_string());

    emit(progress, 0.0, "walking");
    std::vector<nexus::fs::FileEntry> files;
    const auto walk = nexus::fs::walk(
        root, rules, {},
        [&](const nexus::fs::FileEntry& entry) {
            if (is_indexable(entry.path)) {
                files.push_back(entry);
            }
        },
        cancelled);
    summary.files_seen = walk.files;
    summary.cancelled = walk.cancelled;

    for (std::size_t i = 0; i < files.size(); ++i) {
        if (cancelled && cancelled()) {
            summary.cancelled = true;
            break;
        }
        const nexus::fs::FileEntry& entry = files[i];
        const auto text = read_text(entry.path);
        if (!text) {
            ++summary.files_skipped;
            continue;
        }

        const auto freqs = term_frequencies(*text);
        std::uint32_t total = 0;
        for (const auto& [term, tf] : freqs) {
            total += tf;
        }

        const std::string path = entry.path.generic_string();
        const std::int64_t doc_id = repo_->upsert_file(path, entry.size, {}, total);
        repo_->replace_postings(doc_id, freqs);
        index_.add_document_postings(static_cast<nexus::search::DocId>(doc_id), freqs);
        ++summary.files_indexed;

        if (!files.empty()) {
            emit(progress, static_cast<double>(i + 1) / static_cast<double>(files.size()),
                 "indexing");
        }
    }

    repo_->finish_index_job(job_id, summary.cancelled ? "cancelled" : "completed",
                            summary.files_indexed);
    emit(progress, 1.0, "done");
    return summary;
}

void SearchIndexer::remove_path(const fs::path& path) {
    const std::string key = path.generic_string();
    if (const auto file = repo_->find_by_path(key)) {
        index_.remove_document(static_cast<nexus::search::DocId>(file->id));
        repo_->remove_file(key);
    }
}

std::vector<QueryResult> SearchIndexer::query(std::string_view text, std::size_t limit) const {
    const auto terms = nexus::search::tokenize_terms(text, {});
    std::vector<QueryResult> results;
    for (const auto& hit : index_.search(text, limit)) {
        const auto path = repo_->path_of(static_cast<std::int64_t>(hit.id));
        if (!path) {
            continue;
        }
        QueryResult result;
        result.path = *path;
        result.score = hit.score;
        result.matched_terms = hit.matched_terms;
        if (const auto body = read_text(*path, 8u << 10)) {
            result.snippet = nexus::search::make_snippet(*body, terms, 220);
        }
        results.push_back(std::move(result));
    }
    return results;
}

} // namespace nexus::module::search
