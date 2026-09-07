#include "nexus/db/database.hpp"
#include "nexus/db/statement.hpp"
#include "nexus/db/transaction.hpp"

#include <catch2/catch_test_macros.hpp>

using nexus::db::Database;
using nexus::db::Transaction;

namespace {

int row_count(Database& db) {
    auto stmt = db.prepare("SELECT COUNT(*) FROM t");
    stmt.step();
    return static_cast<int>(stmt.column_int64(0));
}

Database make_db() {
    Database db = Database::open_in_memory();
    db.execute("CREATE TABLE t (v INTEGER)");
    return db;
}

} // namespace

TEST_CASE("committed transaction persists writes", "[db][transaction]") {
    Database db = make_db();
    {
        Transaction tx(db);
        db.execute("INSERT INTO t VALUES (1)");
        db.execute("INSERT INTO t VALUES (2)");
        tx.commit();
    }
    REQUIRE(row_count(db) == 2);
}

TEST_CASE("transaction rolls back when not committed", "[db][transaction]") {
    Database db = make_db();
    {
        Transaction tx(db);
        db.execute("INSERT INTO t VALUES (1)");
    }
    REQUIRE(row_count(db) == 0);
}

TEST_CASE("transaction rolls back on exception", "[db][transaction]") {
    Database db = make_db();
    try {
        Transaction tx(db);
        db.execute("INSERT INTO t VALUES (1)");
        throw std::runtime_error("boom");
    } catch (const std::runtime_error&) {
        // expected
    }
    REQUIRE(row_count(db) == 0);
}
