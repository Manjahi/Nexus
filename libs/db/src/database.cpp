#include "nexus/db/database.hpp"

#include "nexus/db/error.hpp"

#include <sqlite3.h>

#include <string>
#include <utility>

namespace nexus::db {

namespace {

[[noreturn]] void throw_error(sqlite3* db, std::string sql = {}) {
    const int code = db ? sqlite3_extended_errcode(db) : SQLITE_ERROR;
    const char* msg = db ? sqlite3_errmsg(db) : "unknown error";
    throw DbError(code, msg ? msg : "unknown error", std::move(sql));
}

void configure(sqlite3* db, bool file_backed) {
    char* err = nullptr;
    const char* pragmas = file_backed
        ? "PRAGMA foreign_keys=ON;"
          "PRAGMA busy_timeout=5000;"
          "PRAGMA journal_mode=WAL;"
          "PRAGMA synchronous=NORMAL;"
        : "PRAGMA foreign_keys=ON;"
          "PRAGMA busy_timeout=5000;";
    if (sqlite3_exec(db, pragmas, nullptr, nullptr, &err) != SQLITE_OK) {
        const std::string message = err ? err : "failed to apply pragmas";
        sqlite3_free(err);
        const int code = sqlite3_extended_errcode(db);
        sqlite3_close(db);
        throw DbError(code, message);
    }
}

sqlite3* open_handle(const char* uri, bool file_backed) {
    sqlite3* db = nullptr;
    const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
    if (sqlite3_open_v2(uri, &db, flags, nullptr) != SQLITE_OK) {
        DbError error(db ? sqlite3_extended_errcode(db) : SQLITE_CANTOPEN,
                      db ? sqlite3_errmsg(db) : "cannot open database");
        sqlite3_close(db);
        throw error;
    }
    configure(db, file_backed);
    return db;
}

} // namespace

Database::Database(sqlite3* db) noexcept : db_(db) {}

Database::Database(Database&& other) noexcept : db_(std::exchange(other.db_, nullptr)) {}

Database& Database::operator=(Database&& other) noexcept {
    if (this != &other) {
        if (db_) {
            sqlite3_close(db_);
        }
        db_ = std::exchange(other.db_, nullptr);
    }
    return *this;
}

Database::~Database() {
    if (db_) {
        sqlite3_close(db_);
    }
}

Database Database::open(const std::filesystem::path& path) {
    const std::u8string utf8 = path.u8string();
    return Database(open_handle(reinterpret_cast<const char*>(utf8.c_str()), true));
}

Database Database::open_in_memory() {
    return Database(open_handle(":memory:", false));
}

void Database::execute(std::string_view sql) {
    const std::string owned(sql);
    char* err = nullptr;
    if (sqlite3_exec(db_, owned.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        const std::string message = err ? err : "exec failed";
        sqlite3_free(err);
        throw DbError(sqlite3_extended_errcode(db_), message, owned);
    }
}

Statement Database::prepare(std::string_view sql) {
    sqlite3_stmt* stmt = nullptr;
    const int rc = sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()), &stmt, nullptr);
    if (rc != SQLITE_OK) {
        throw_error(db_, std::string(sql));
    }
    return Statement(db_, stmt);
}

std::int64_t Database::last_insert_rowid() const noexcept {
    return sqlite3_last_insert_rowid(db_);
}

int Database::changes() const noexcept {
    return sqlite3_changes(db_);
}

} // namespace nexus::db
