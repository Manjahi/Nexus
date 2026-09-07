#pragma once

// Internal helpers shared by the repository implementations. Not part of the
// public API (kept out of the header FILE_SET).

#include <optional>
#include <string>

#include "nexus/core/time.hpp"
#include "nexus/db/statement.hpp"

namespace nexus::services::detail {

inline void bind_text_or_null(nexus::db::Statement& stmt, int index, const std::string& value) {
    if (value.empty()) {
        stmt.bind(index, nullptr);
    } else {
        stmt.bind(index, value);
    }
}

inline void bind_time_or_null(nexus::db::Statement& stmt, int index,
                              const std::optional<nexus::core::Timestamp>& value) {
    if (value.has_value()) {
        stmt.bind(index, nexus::core::to_iso8601(*value));
    } else {
        stmt.bind(index, nullptr);
    }
}

inline std::optional<nexus::core::Timestamp> column_time_or_null(nexus::db::Statement& stmt,
                                                                int col) {
    if (stmt.column_is_null(col)) {
        return std::nullopt;
    }
    return nexus::core::from_iso8601(stmt.column_text(col));
}

} // namespace nexus::services::detail
