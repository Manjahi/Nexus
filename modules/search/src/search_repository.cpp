#include "nexus/module/search/search_repository.hpp"

#include "nexus/core/time.hpp"
#include "nexus/db/database.hpp"
#include "nexus/db/statement.hpp"
#include "nexus/db/transaction.hpp"

#include <string>
#include <utility>

namespace nexus::module::search {

std::int64_t SearchRepository::upsert_file(std::string_view path, std::uint64_t size,
                                           std::string_view mtime, std::uint32_t term_count) {
    const std::string now = nexus::core::to_iso8601(nexus::core::now());
    nexus::db::Statement stmt = db_->prepare(
        "INSERT INTO indexed_files (path, size, mtime, indexed_at, term_count) "
        "VALUES (?, ?, ?, ?, ?) "
        "ON CONFLICT(path) DO UPDATE SET size = excluded.size, mtime = excluded.mtime, "
        "indexed_at = excluded.indexed_at, term_count = excluded.term_count");
    stmt.bind(1, path);
    stmt.bind(2, static_cast<std::int64_t>(size));
    if (mtime.empty()) {
        stmt.bind(3, nullptr);
    } else {
        stmt.bind(3, mtime);
    }
    stmt.bind(4, now);
    stmt.bind(5, static_cast<std::int64_t>(term_count));
    stmt.step();

    nexus::db::Statement id = db_->prepare("SELECT id FROM indexed_files WHERE path = ?");
    id.bind(1, path);
    id.step();
    return id.column_int64(0);
}

std::optional<IndexedFile> SearchRepository::find_by_path(std::string_view path) const {
    nexus::db::Statement stmt =
        db_->prepare("SELECT id, path, size, mtime, term_count FROM indexed_files WHERE path = ?");
    stmt.bind(1, path);
    if (!stmt.step()) {
        return std::nullopt;
    }
    IndexedFile file;
    file.id = stmt.column_int64(0);
    file.path = stmt.column_text(1);
    file.size = static_cast<std::uint64_t>(stmt.column_int64(2));
    file.mtime = stmt.column_is_null(3) ? std::string{} : stmt.column_text(3);
    file.term_count = static_cast<std::uint32_t>(stmt.column_int64(4));
    return file;
}

std::optional<std::string> SearchRepository::path_of(std::int64_t doc_id) const {
    nexus::db::Statement stmt = db_->prepare("SELECT path FROM indexed_files WHERE id = ?");
    stmt.bind(1, doc_id);
    if (!stmt.step()) {
        return std::nullopt;
    }
    return stmt.column_text(0);
}

std::vector<IndexedFile> SearchRepository::all_files() const {
    nexus::db::Statement stmt =
        db_->prepare("SELECT id, path, size, mtime, term_count FROM indexed_files");
    std::vector<IndexedFile> out;
    while (stmt.step()) {
        IndexedFile file;
        file.id = stmt.column_int64(0);
        file.path = stmt.column_text(1);
        file.size = static_cast<std::uint64_t>(stmt.column_int64(2));
        file.mtime = stmt.column_is_null(3) ? std::string{} : stmt.column_text(3);
        file.term_count = static_cast<std::uint32_t>(stmt.column_int64(4));
        out.push_back(std::move(file));
    }
    return out;
}

bool SearchRepository::remove_file(std::string_view path) {
    nexus::db::Statement stmt = db_->prepare("DELETE FROM indexed_files WHERE path = ?");
    stmt.bind(1, path);
    stmt.step();
    return stmt.changes() > 0;
}

void SearchRepository::replace_postings(std::int64_t doc_id,
                                        const std::map<std::string, std::uint32_t>& terms) {
    nexus::db::Transaction tx(*db_);
    replace_postings_in_batch(doc_id, terms);
    tx.commit();
}

std::unique_ptr<nexus::db::Transaction> SearchRepository::begin_batch() {
    return std::make_unique<nexus::db::Transaction>(*db_);
}

void SearchRepository::replace_postings_in_batch(
    std::int64_t doc_id, const std::map<std::string, std::uint32_t>& terms) {
    nexus::db::Statement clear = db_->prepare("DELETE FROM search_terms WHERE doc_id = ?");
    clear.bind(1, doc_id);
    clear.step();

    nexus::db::Statement insert =
        db_->prepare("INSERT INTO search_terms (term, doc_id, tf) VALUES (?, ?, ?)");
    for (const auto& [term, tf] : terms) {
        insert.bind(1, term);
        insert.bind(2, doc_id);
        insert.bind(3, static_cast<std::int64_t>(tf));
        insert.step();
        insert.reset();
    }
}

std::vector<StoredPosting> SearchRepository::all_postings() const {
    nexus::db::Statement stmt = db_->prepare("SELECT doc_id, term, tf FROM search_terms");
    std::vector<StoredPosting> out;
    while (stmt.step()) {
        out.push_back({stmt.column_int64(0), stmt.column_text(1),
                       static_cast<std::uint32_t>(stmt.column_int64(2))});
    }
    return out;
}

std::size_t SearchRepository::file_count() const {
    nexus::db::Statement stmt = db_->prepare("SELECT COUNT(*) FROM indexed_files");
    stmt.step();
    return static_cast<std::size_t>(stmt.column_int64(0));
}

nexus::core::Uuid SearchRepository::begin_index_job(std::string_view root) {
    const nexus::core::Uuid id = nexus::core::Uuid::generate();
    nexus::db::Statement stmt = db_->prepare(
        "INSERT INTO index_jobs (id, root, started_at, state) VALUES (?, ?, ?, 'running')");
    stmt.bind(1, id.to_string());
    stmt.bind(2, root);
    stmt.bind(3, nexus::core::to_iso8601(nexus::core::now()));
    stmt.step();
    return id;
}

void SearchRepository::finish_index_job(const nexus::core::Uuid& id, std::string_view state,
                                        std::uint64_t files_indexed) {
    nexus::db::Statement stmt = db_->prepare(
        "UPDATE index_jobs SET finished_at = ?, state = ?, files_indexed = ? WHERE id = ?");
    stmt.bind(1, nexus::core::to_iso8601(nexus::core::now()));
    stmt.bind(2, state);
    stmt.bind(3, static_cast<std::int64_t>(files_indexed));
    stmt.bind(4, id.to_string());
    stmt.step();
}

} // namespace nexus::module::search
