#include "nexus/db/migration.hpp"

#include "nexus/core/time.hpp"
#include "nexus/db/database.hpp"
#include "nexus/db/statement.hpp"
#include "nexus/db/transaction.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace nexus::db {

namespace {

constexpr std::string_view kCreateBookkeeping = R"sql(
CREATE TABLE IF NOT EXISTS schema_migrations (
    component   TEXT NOT NULL,
    version     INTEGER NOT NULL,
    name        TEXT NOT NULL,
    applied_at  TEXT NOT NULL,
    PRIMARY KEY (component, version)
);
)sql";

} // namespace

int schema_version(Database& db, std::string_view component) {
    db.execute(kCreateBookkeeping);
    Statement stmt =
        db.prepare("SELECT COALESCE(MAX(version), 0) FROM schema_migrations WHERE component = ?");
    stmt.bind(1, component);
    stmt.step();
    return static_cast<int>(stmt.column_int64(0));
}

int migrate(Database& db, std::string_view component, std::span<const Migration> migrations) {
    db.execute(kCreateBookkeeping);

    std::vector<Migration> ordered(migrations.begin(), migrations.end());
    std::sort(ordered.begin(), ordered.end(),
              [](const Migration& a, const Migration& b) { return a.version < b.version; });

    int current = schema_version(db, component);

    for (const Migration& m : ordered) {
        if (m.version <= current) {
            continue;
        }

        Transaction tx(db);
        db.execute(m.up_sql);

        Statement record =
            db.prepare("INSERT INTO schema_migrations (component, version, name, applied_at) "
                       "VALUES (?, ?, ?, ?)");
        record.bind(1, component);
        record.bind(2, m.version);
        record.bind(3, m.name);
        record.bind(4, nexus::core::to_iso8601(nexus::core::now()));
        record.step();

        tx.commit();
        current = m.version;
    }

    return current;
}

} // namespace nexus::db
