#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

#include "nexus/db/statement.hpp"

struct sqlite3;

namespace nexus::db {

/// Owning handle to a SQLite connection.
///
/// On open the connection is configured with foreign keys enforced, a busy
/// timeout, WAL journalling (file databases), and NORMAL synchronous mode.
class Database {
public:
    [[nodiscard]] static Database open(const std::filesystem::path& path);
    [[nodiscard]] static Database open_in_memory();

    Database(Database&& other) noexcept;
    Database& operator=(Database&& other) noexcept;
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    ~Database();

    /// Runs one or more statements that produce no result rows.
    void execute(std::string_view sql);

    [[nodiscard]] Statement prepare(std::string_view sql);

    [[nodiscard]] std::int64_t last_insert_rowid() const noexcept;
    [[nodiscard]] int changes() const noexcept;

    [[nodiscard]] sqlite3* handle() const noexcept { return db_; }

private:
    explicit Database(sqlite3* db) noexcept;

    sqlite3* db_{nullptr};
};

} // namespace nexus::db
