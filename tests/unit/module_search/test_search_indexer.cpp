#include "nexus/module/search/content_reader.hpp"
#include "nexus/module/search/search_indexer.hpp"
#include "nexus/module/search/search_repository.hpp"

#include "nexus/db/database.hpp"
#include "nexus/db/migration.hpp"
#include "nexus/fs/exclusion_rules.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

namespace fs = std::filesystem;
using namespace nexus::module::search;

namespace {

struct Corpus {
    fs::path root;
    Corpus() {
        const auto tag = std::chrono::steady_clock::now().time_since_epoch().count();
        root = fs::temp_directory_path() / ("nexuspc_search_" + std::to_string(tag));
        fs::create_directories(root / "notes");
        write(root / "notes" / "backup.md",
              "# Backup design\nThe backup engine writes content-addressed blobs. "
              "Snapshots are incremental.");
        write(root / "notes" / "search.txt",
              "The search index uses BM25 ranking over an inverted index of terms.");
        write(root / "readme.md", "NexusPC bundles storage, backup, search and more.");
        write(root / "photo.jpg", std::string(400, '\xFF')); // binary, skipped
        write(root / "data.bin", std::string("\0\0\0\0not text", 12));
    }
    ~Corpus() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    static void write(const fs::path& p, std::string_view content) {
        std::ofstream out(p, std::ios::binary);
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
    }
};

nexus::db::Database migrated_db() {
    auto db = nexus::db::Database::open_in_memory();
    nexus::db::migrate(db, "search", search_migrations());
    return db;
}

} // namespace

TEST_CASE("content_reader accepts text and rejects binaries", "[search][reader]") {
    REQUIRE(is_indexable("a/b/notes.md"));
    REQUIRE(is_indexable("Main.CPP"));
    REQUIRE_FALSE(is_indexable("image.png"));
    REQUIRE_FALSE(is_indexable("archive.zip"));
}

TEST_CASE("index_tree indexes only text files and ranks queries", "[search][indexer]") {
    Corpus c;
    auto db = migrated_db();
    SearchRepository repo(db);
    SearchIndexer indexer(repo);

    const auto summary = indexer.index_tree(c.root, nexus::fs::ExclusionRules{});
    REQUIRE_FALSE(summary.cancelled);
    REQUIRE(summary.files_indexed == 3); // the two .md and one .txt
    REQUIRE(indexer.indexed_documents() == 3);

    const auto backup_hits = indexer.query("backup incremental");
    REQUIRE_FALSE(backup_hits.empty());
    REQUIRE(backup_hits.front().path.find("backup.md") != std::string::npos);
    REQUIRE_FALSE(backup_hits.front().snippet.empty());

    const auto bm25 = indexer.query("bm25 ranking");
    REQUIRE_FALSE(bm25.empty());
    REQUIRE(bm25.front().path.find("search.txt") != std::string::npos);

    REQUIRE(indexer.query("nonexistentterm").empty());
}

TEST_CASE("the index is rebuilt from the database on construction", "[search][indexer]") {
    Corpus c;
    auto db = migrated_db();
    {
        SearchRepository repo(db);
        SearchIndexer indexer(repo);
        indexer.index_tree(c.root, nexus::fs::ExclusionRules{});
    }

    SearchRepository repo2(db);
    SearchIndexer reopened(repo2); // loads persisted postings
    REQUIRE(reopened.indexed_documents() == 3);
    REQUIRE_FALSE(reopened.query("storage").empty());
}

TEST_CASE("re-indexing a changed file replaces its terms", "[search][indexer]") {
    Corpus c;
    auto db = migrated_db();
    SearchRepository repo(db);
    SearchIndexer indexer(repo);
    indexer.index_tree(c.root, nexus::fs::ExclusionRules{});
    REQUIRE_FALSE(indexer.query("incremental").empty());

    Corpus::write(c.root / "notes" / "backup.md", "This note now talks only about elephants.");
    indexer.index_tree(c.root, nexus::fs::ExclusionRules{});

    REQUIRE(indexer.indexed_documents() == 3);
    REQUIRE(indexer.query("incremental").empty());
    REQUIRE_FALSE(indexer.query("elephants").empty());
}

TEST_CASE("remove_path drops a file from the index", "[search][indexer]") {
    Corpus c;
    auto db = migrated_db();
    SearchRepository repo(db);
    SearchIndexer indexer(repo);
    indexer.index_tree(c.root, nexus::fs::ExclusionRules{});

    indexer.remove_path(c.root / "readme.md");
    REQUIRE(indexer.indexed_documents() == 2);
    REQUIRE(repo.file_count() == 2);
}
