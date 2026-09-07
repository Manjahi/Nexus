#include "nexus/db/migration.hpp"

#include "nexus/db/database.hpp"
#include "nexus/db/statement.hpp"
#include "nexus/db/transaction.hpp"

#include "nexus/core/time.hpp"

#include <algorithm>
#include <vector>

namespace nexus::db {

namespace {

constexpr std::string_view kCreateBookkeeping = R"sql(
CREATE TABLE IF NOT EXISTS schema_migrations (
    version     INTEGER PRIMARY KEY,
    name        TEXT NOT NULL,
    applied_at  TEXT NOT NULL
);
)sql";

} // namespace

int schema_version(Database& db) {
    db.execute(kCreateBookkeeping);
    Statement stmt = db.prepare("SELECT COALESCE(MAX(version), 0) FROM schema_migrations");
    stmt.step();
    return static_cast<int>(stmt.column_int64(0));
}

int migrate(Database& db, std::span<const Migration> migrations) {
    db.execute(kCreateBookkeeping);

    std::vector<Migration> ordered(migrations.begin(), migrations.end());
    std::sort(ordered.begin(), ordered.end(),
              [](const Migration& a, const Migration& b) { return a.version < b.version; });

    int current = schema_version(db);

    for (const Migration& m : ordered) {
        if (m.version <= current) {
            continue;
        }

        Transaction tx(db);
        db.execute(m.up_sql);

        Statement record = db.prepare(
            "INSERT INTO schema_migrations (version, name, applied_at) VALUES (?, ?, ?)");
        record.bind(1, m.version);
        record.bind(2, m.name);
        record.bind(3, nexus::core::to_iso8601(nexus::core::now()));
        record.step();

        tx.commit();
        current = m.version;
    }

    return current;
}

} // namespace nexus::db
