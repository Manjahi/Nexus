#include "nexus/db/statement.hpp"

#include "nexus/db/error.hpp"

#include <sqlite3.h>

#include <string>
#include <utility>

namespace nexus::db {

namespace {

[[noreturn]] void throw_error(sqlite3* db) {
    const int code = sqlite3_extended_errcode(db);
    const char* msg = sqlite3_errmsg(db);
    throw DbError(code, msg ? msg : "unknown error");
}

void check_bind(sqlite3* db, int rc) {
    if (rc != SQLITE_OK) {
        throw_error(db);
    }
}

} // namespace

Statement::Statement(sqlite3* db, sqlite3_stmt* stmt) noexcept : db_(db), stmt_(stmt) {}

Statement::Statement(Statement&& other) noexcept
    : db_(std::exchange(other.db_, nullptr)), stmt_(std::exchange(other.stmt_, nullptr)) {}

Statement& Statement::operator=(Statement&& other) noexcept {
    if (this != &other) {
        if (stmt_) {
            sqlite3_finalize(stmt_);
        }
        db_ = std::exchange(other.db_, nullptr);
        stmt_ = std::exchange(other.stmt_, nullptr);
    }
    return *this;
}

Statement::~Statement() {
    if (stmt_) {
        sqlite3_finalize(stmt_);
    }
}

int Statement::parameter_index(std::string_view name) const {
    const std::string owned(name);
    const int index = sqlite3_bind_parameter_index(stmt_, owned.c_str());
    if (index == 0) {
        throw DbError(SQLITE_RANGE, "no such bind parameter: " + owned);
    }
    return index;
}

Statement& Statement::bind(int index, std::int64_t value) {
    check_bind(db_, sqlite3_bind_int64(stmt_, index, value));
    return *this;
}

Statement& Statement::bind(int index, double value) {
    check_bind(db_, sqlite3_bind_double(stmt_, index, value));
    return *this;
}

Statement& Statement::bind(int index, std::string_view value) {
    check_bind(db_, sqlite3_bind_text(stmt_, index, value.data(),
                                      static_cast<int>(value.size()), SQLITE_TRANSIENT));
    return *this;
}

Statement& Statement::bind(int index, std::nullptr_t) {
    check_bind(db_, sqlite3_bind_null(stmt_, index));
    return *this;
}

Statement& Statement::bind(std::string_view name, std::int64_t value) {
    return bind(parameter_index(name), value);
}

Statement& Statement::bind(std::string_view name, double value) {
    return bind(parameter_index(name), value);
}

Statement& Statement::bind(std::string_view name, std::string_view value) {
    return bind(parameter_index(name), value);
}

Statement& Statement::bind(std::string_view name, std::nullptr_t) {
    return bind(parameter_index(name), nullptr);
}

bool Statement::step() {
    const int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) {
        return true;
    }
    if (rc == SQLITE_DONE) {
        return false;
    }
    throw_error(db_);
}

void Statement::reset() {
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
}

int Statement::column_count() const {
    return sqlite3_column_count(stmt_);
}

bool Statement::column_is_null(int col) const {
    return sqlite3_column_type(stmt_, col) == SQLITE_NULL;
}

std::int64_t Statement::column_int64(int col) const {
    return sqlite3_column_int64(stmt_, col);
}

double Statement::column_double(int col) const {
    return sqlite3_column_double(stmt_, col);
}

std::string Statement::column_text(int col) const {
    const auto* text = sqlite3_column_text(stmt_, col);
    if (text == nullptr) {
        return {};
    }
    const auto length = static_cast<std::size_t>(sqlite3_column_bytes(stmt_, col));
    return std::string(reinterpret_cast<const char*>(text), length);
}

} // namespace nexus::db
