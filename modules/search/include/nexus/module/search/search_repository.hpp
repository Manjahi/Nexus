#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "nexus/core/id.hpp"

namespace nexus::db {
class Database;
struct Migration;
}

namespace nexus::module::search {

[[nodiscard]] std::span<const nexus::db::Migration> search_migrations();

struct IndexedFile {
    std::int64_t id = 0;
    std::string path;
    std::uint64_t size = 0;
    std::string mtime;
    std::uint32_t term_count = 0;
};

struct StoredPosting {
    std::int64_t doc_id = 0;
    std::string term;
    std::uint32_t tf = 0;
};

class SearchRepository {
public:
    explicit SearchRepository(nexus::db::Database& db) noexcept : db_(&db) {}

    /// Finds the row for `path` or inserts it; updates metadata. Returns the id.
    std::int64_t upsert_file(std::string_view path, std::uint64_t size, std::string_view mtime,
                             std::uint32_t term_count);
    [[nodiscard]] std::optional<IndexedFile> find_by_path(std::string_view path) const;
    [[nodiscard]] std::optional<std::string> path_of(std::int64_t doc_id) const;
    bool remove_file(std::string_view path);

    void replace_postings(std::int64_t doc_id, const std::map<std::string, std::uint32_t>& terms);

    /// Every posting, for rebuilding the in-memory index.
    [[nodiscard]] std::vector<StoredPosting> all_postings() const;
    [[nodiscard]] std::size_t file_count() const;

    nexus::core::Uuid begin_index_job(std::string_view root);
    void finish_index_job(const nexus::core::Uuid& id, std::string_view state,
                          std::uint64_t files_indexed);

private:
    nexus::db::Database* db_;
};

} // namespace nexus::module::search
