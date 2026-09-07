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

/// Applies every migration whose version is greater than the highest already
/// recorded in `schema_migrations`, in ascending order, each in its own
/// transaction. Creates `schema_migrations` if needed. Idempotent.
///
/// Returns the schema version after migrating. Throws DbError on failure,
/// leaving the last good migration committed.
int migrate(Database& db, std::span<const Migration> migrations);

/// Current schema version (highest recorded), or 0 if never migrated.
[[nodiscard]] int schema_version(Database& db);

/// Built-in migrations for the shared suite metadata (spec section 7).
[[nodiscard]] std::span<const Migration> core_migrations();

} // namespace nexus::db
