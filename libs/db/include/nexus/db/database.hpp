#pragma once

#include "nexus/db/statement.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string_view>

struct sqlite3;

namespace nexus::db {

class Transaction;

/// Owning handle to a SQLite connection.
///
/// On open the connection is configured with foreign keys enforced, a busy
/// timeout, WAL journalling (file databases), and NORMAL synchronous mode.
///
/// All access is serialised through an internal recursive mutex, so one
/// `Database` may be shared across threads: statements and transactions from
/// different threads run one at a time, and a `Transaction` holds the lock for
/// its whole scope.
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

    /// Rows changed / rowid inserted by the most recent statement on this
    /// connection. Only meaningful while the caller holds a Transaction (or is
    /// otherwise the sole user); prefer Statement::changes() /
    /// Statement::last_insert_rowid(), which capture the value under the step
    /// lock.
    [[nodiscard]] std::int64_t last_insert_rowid() const;
    [[nodiscard]] int changes() const;

    [[nodiscard]] sqlite3* handle() const noexcept { return db_; }

private:
    explicit Database(sqlite3* db);
    friend class Transaction;

    [[nodiscard]] std::recursive_mutex& sync() const noexcept { return *mutex_; }

    sqlite3* db_{nullptr};
    std::unique_ptr<std::recursive_mutex> mutex_{std::make_unique<std::recursive_mutex>()};
};

} // namespace nexus::db
