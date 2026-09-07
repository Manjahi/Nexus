#include "nexus/db/error.hpp"

#include <utility>

namespace nexus::db {

namespace {

std::string compose(int code, const std::string& message, const std::string& sql) {
    std::string out = "sqlite error " + std::to_string(code) + ": " + message;
    if (!sql.empty()) {
        out += " [" + sql + "]";
    }
    return out;
}

} // namespace

DbError::DbError(int code, std::string message, std::string sql)
    : std::runtime_error(compose(code, message, sql)),
      code_(code),
      sql_(std::move(sql)) {}

} // namespace nexus::db
