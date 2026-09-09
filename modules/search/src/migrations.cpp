#include "nexus/module/search/search_repository.hpp"

#include <array>

#include "nexus/db/migration.hpp"

namespace nexus::module::search {

namespace {

// Spec section 7, Search tables. Postings live in search_terms; the in-memory
// index is rebuilt from them at startup.
constexpr std::string_view kSchemaUp = R"sql(
CREATE TABLE indexed_files (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    path       TEXT NOT NULL UNIQUE,
    size       INTEGER NOT NULL DEFAULT 0,
    mtime      TEXT,
    indexed_at TEXT NOT NULL,
    term_count INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE search_terms (
    term   TEXT NOT NULL,
    doc_id INTEGER NOT NULL REFERENCES indexed_files(id) ON DELETE CASCADE,
    tf     INTEGER NOT NULL,
    PRIMARY KEY (term, doc_id)
);
CREATE INDEX idx_search_terms_doc ON search_terms(doc_id);

CREATE TABLE index_jobs (
    id            TEXT PRIMARY KEY,
    root          TEXT NOT NULL,
    started_at    TEXT NOT NULL,
    finished_at   TEXT,
    files_indexed INTEGER NOT NULL DEFAULT 0,
    state         TEXT NOT NULL
);
)sql";

constexpr std::array<nexus::db::Migration, 1> kMigrations{{
    {1, "search_schema", kSchemaUp},
}};

} // namespace

std::span<const nexus::db::Migration> search_migrations() {
    return kMigrations;
}

} // namespace nexus::module::search
