#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

struct sqlite3;
struct sqlite3_stmt;

namespace nexus::db {

class Database;

/// RAII wrapper around a prepared statement.
///
/// Bind parameters are 1-based (SQLite convention); result columns are 0-based.
/// Bound text/blob values are copied, so callers need not keep the source alive.
/// `step()` is serialised on the owning connection's mutex.
class Statement {
public:
    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&& other) noexcept;
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    ~Statement();

    Statement& bind(int index, std::int64_t value);
    Statement& bind(int index, int value) { return bind(index, static_cast<std::int64_t>(value)); }
    Statement& bind(int index, double value);
    Statement& bind(int index, std::string_view value);
    Statement& bind(int index, std::nullptr_t);

    Statement& bind(std::string_view name, std::int64_t value);
    Statement& bind(std::string_view name, int value) {
        return bind(name, static_cast<std::int64_t>(value));
    }
    Statement& bind(std::string_view name, double value);
    Statement& bind(std::string_view name, std::string_view value);
    Statement& bind(std::string_view name, std::nullptr_t);

    /// Advances to the next row. Returns true while a row is available, false
    /// once the statement is done. On completion, captures changes() and
    /// last_insert_rowid() for this connection under the step lock.
    bool step();

    /// Resets execution and clears all bindings so the statement can be reused.
    void reset();

    /// Rows changed / rowid inserted, as observed when this statement last
    /// finished stepping.
    [[nodiscard]] int changes() const noexcept { return changes_; }
    [[nodiscard]] std::int64_t last_insert_rowid() const noexcept { return last_rowid_; }

    [[nodiscard]] int column_count() const;
    [[nodiscard]] bool column_is_null(int col) const;
    [[nodiscard]] std::int64_t column_int64(int col) const;
    [[nodiscard]] double column_double(int col) const;
    [[nodiscard]] std::string column_text(int col) const;

private:
    friend class Database;
    Statement(sqlite3* db, sqlite3_stmt* stmt, std::recursive_mutex* mutex) noexcept;

    [[nodiscard]] int parameter_index(std::string_view name) const;

    sqlite3* db_{nullptr};
    sqlite3_stmt* stmt_{nullptr};
    std::recursive_mutex* mutex_{nullptr};
    int changes_{0};
    std::int64_t last_rowid_{0};
};

} // namespace nexus::db
