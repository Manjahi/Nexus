#pragma once

#include <span>
#include <string_view>

namespace nexus::db {

class Database;

/// One forward schema change. `up_sql` may contain multiple statements; it is
/// run inside a transaction together with the bookkeeping insert.
struct Migration {
    int version;
    std::string_view name;
    std::string_view up_sql;
};

/// Applies every migration for `component` whose version is greater than the
/// highest already recorded, in ascending order, each in its own transaction.
/// Creates `schema_migrations` if needed. Idempotent per component; different
/// components keep independent version lines.
///
/// Returns the component's schema version after migrating. Throws DbError on
/// failure, leaving the last good migration committed.
int migrate(Database& db, std::string_view component, std::span<const Migration> migrations);

/// Current schema version for `component` (highest recorded), or 0.
[[nodiscard]] int schema_version(Database& db, std::string_view component);

/// Built-in migrations for the shared suite metadata (spec section 7). Apply
/// under the component name "core".
[[nodiscard]] std::span<const Migration> core_migrations();

} // namespace nexus::db
