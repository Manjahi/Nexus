#include "nexus/module/search/search_indexer.hpp"

#include <algorithm>
#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "nexus/core/time.hpp"
#include "nexus/db/transaction.hpp"
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

// Commits every N documents instead of one fsync per document - the single
// biggest cost in indexing a large tree. Bounded so a cancel/crash mid-run
// loses at most one batch's worth of work, not the whole run.
constexpr std::size_t kBatchSize = 200;

std::map<std::string, std::uint32_t> term_frequencies(std::string_view text) {
    std::map<std::string, std::uint32_t> freq;
    for (const auto& token : nexus::search::tokenize(text, {})) {
        ++freq[token.term];
    }
    return freq;
}

// Portable enough to detect "did this file change" - same conversion backup
// uses for its own file mtimes (backup_engine.cpp's iso_mtime).
std::string iso_mtime(fs::file_time_type when) {
    const auto sys = std::chrono::clock_cast<std::chrono::system_clock>(when);
    return nexus::core::to_iso8601(sys);
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

    // Previously-indexed files under this root, keyed by path - lets this
    // pass skip unchanged files without re-reading them, and afterwards spot
    // any that vanished from disk since the last index (SearchIndexer used
    // to have neither: index_tree() re-read/re-tokenized every file every
    // time, and a deleted/moved file stayed a dead link in results forever).
    const std::string root_prefix = root.generic_string() + "/";
    std::unordered_map<std::string, IndexedFile> previously_indexed;
    for (IndexedFile& file : repo_->all_files()) {
        if (file.path.starts_with(root_prefix)) {
            previously_indexed.emplace(file.path, std::move(file));
        }
    }
    std::unordered_set<std::string> seen_paths;

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

    std::unique_ptr<nexus::db::Transaction> batch;
    std::size_t since_commit = 0;

    for (std::size_t i = 0; i < files.size(); ++i) {
        if (cancelled && cancelled()) {
            summary.cancelled = true;
            break;
        }
        const nexus::fs::FileEntry& entry = files[i];
        const std::string path = entry.path.generic_string();
        seen_paths.insert(path);
        const std::string mtime = iso_mtime(entry.last_write_time);

        if (const auto it = previously_indexed.find(path); it != previously_indexed.end()) {
            if (it->second.size == entry.size && it->second.mtime == mtime) {
                ++summary.files_unchanged;
                continue; // same size+mtime as last time - skip the read/tokenize
            }
        }

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

        if (!batch) {
            batch = repo_->begin_batch();
        }
        const std::int64_t doc_id = repo_->upsert_file(path, entry.size, mtime, total);
        repo_->replace_postings_in_batch(doc_id, freqs);
        index_.add_document_postings(static_cast<nexus::search::DocId>(doc_id), freqs);
        ++summary.files_indexed;

        if (++since_commit >= kBatchSize) {
            batch->commit();
            batch.reset();
            since_commit = 0;
        }

        if (!files.empty()) {
            emit(progress, static_cast<double>(i + 1) / static_cast<double>(files.size()),
                 "indexing");
        }
    }
    if (batch) {
        batch->commit();
    }

    // Anything indexed under this root before, but not walked just now, no
    // longer exists (or moved) - drop it so it stops appearing as a dead
    // link in query results.
    if (!summary.cancelled) {
        for (const auto& [path, file] : previously_indexed) {
            if (!seen_paths.contains(path)) {
                index_.remove_document(static_cast<nexus::search::DocId>(file.id));
                repo_->remove_file(path);
                ++summary.files_removed;
            }
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

std::vector<QueryResult> SearchIndexer::query(std::string_view text, std::size_t limit,
                                              const QueryFilter& filter) const {
    const auto terms = nexus::search::tokenize_terms(text, {});

    // A filter rejects some ranked hits, so asking the BM25 index for just
    // `limit` candidates could hand back fewer than `limit` matching results
    // even when more exist further down the ranking - ask for a larger pool
    // up front instead of truncating before the filter ever runs. Capped so
    // a very permissive filter (or none) still costs what it used to.
    const std::size_t candidate_limit =
        filter.active() ? std::clamp<std::size_t>(limit * 20, 200, 2000) : limit;

    std::string wanted_ext = filter.extension;
    std::transform(wanted_ext.begin(), wanted_ext.end(), wanted_ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    std::vector<QueryResult> results;
    for (const auto& hit : index_.search(text, candidate_limit)) {
        const auto path = repo_->path_of(static_cast<std::int64_t>(hit.id));
        if (!path) {
            continue;
        }

        if (!wanted_ext.empty()) {
            std::string ext = std::filesystem::path(*path).extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (ext != wanted_ext) {
                continue;
            }
        }
        if (filter.modified_after) {
            const auto file = repo_->find_by_path(*path);
            const auto mtime = file ? nexus::core::from_iso8601(file->mtime) : std::nullopt;
            if (!mtime || *mtime < *filter.modified_after) {
                continue;
            }
        }

        QueryResult result;
        result.path = *path;
        result.score = hit.score;
        result.matched_terms = hit.matched_terms;
        if (const auto body = read_text(*path, 8u << 10)) {
            result.snippet = nexus::search::make_snippet(*body, terms, 220);
        }
        results.push_back(std::move(result));
        if (results.size() >= limit) {
            break;
        }
    }
    return results;
}

} // namespace nexus::module::search
