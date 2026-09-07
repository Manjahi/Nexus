#pragma once

#include <stdexcept>
#include <string>

namespace nexus::db {

/// Thrown for any SQLite failure. `code()` is the raw SQLite result code, so
/// callers that expect a specific condition (e.g. SQLITE_CONSTRAINT) can catch
/// and inspect it.
class DbError : public std::runtime_error {
public:
    DbError(int code, std::string message, std::string sql = {});

    [[nodiscard]] int code() const noexcept { return code_; }
    [[nodiscard]] const std::string& sql() const noexcept { return sql_; }

private:
    int code_;
    std::string sql_;
};

} // namespace nexus::db
