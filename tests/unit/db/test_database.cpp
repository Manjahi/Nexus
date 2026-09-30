#include "nexus/db/database.hpp"
#include "nexus/db/error.hpp"
#include "nexus/db/statement.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>

using nexus::db::Database;
using nexus::db::DbError;

TEST_CASE("in-memory database round-trips rows", "[db][database]") {
    Database db = Database::open_in_memory();
    db.execute("CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT, score REAL, note TEXT)");

    auto insert = db.prepare("INSERT INTO t (name, score, note) VALUES (?, ?, ?)");
    insert.bind(1, std::string_view{"alice"});
    insert.bind(2, 1.5);
    insert.bind(3, nullptr);
    REQUIRE_FALSE(insert.step());
    REQUIRE(db.last_insert_rowid() == 1);
    REQUIRE(db.changes() == 1);

    auto select = db.prepare("SELECT id, name, score, note FROM t WHERE name = :name");
    select.bind(":name", std::string_view{"alice"});
    REQUIRE(select.step());
    REQUIRE(select.column_int64(0) == 1);
    REQUIRE(select.column_text(1) == "alice");
    REQUIRE(select.column_double(2) == 1.5);
    REQUIRE(select.column_is_null(3));
    REQUIRE_FALSE(select.step());
}

TEST_CASE("statement reset allows reuse", "[db][statement]") {
    Database db = Database::open_in_memory();
    db.execute("CREATE TABLE t (v INTEGER)");

    auto insert = db.prepare("INSERT INTO t (v) VALUES (?)");
    for (int i = 1; i <= 3; ++i) {
        insert.bind(1, i);
        insert.step();
        insert.reset();
    }

    auto count = db.prepare("SELECT COUNT(*), SUM(v) FROM t");
    REQUIRE(count.step());
    REQUIRE(count.column_int64(0) == 3);
    REQUIRE(count.column_int64(1) == 6);
}

TEST_CASE("foreign keys are enforced", "[db][database]") {
    Database db = Database::open_in_memory();
    db.execute("CREATE TABLE parent (id INTEGER PRIMARY KEY);"
               "CREATE TABLE child (id INTEGER PRIMARY KEY, "
               "  pid INTEGER NOT NULL REFERENCES parent(id))");

    auto insert = db.prepare("INSERT INTO child (pid) VALUES (99)");
    REQUIRE_THROWS_AS(insert.step(), DbError);
}

TEST_CASE("invalid SQL throws DbError", "[db][database]") {
    Database db = Database::open_in_memory();
    REQUIRE_THROWS_AS(db.prepare("SELECT * FROM does_not_exist"), DbError);
    REQUIRE_THROWS_AS(db.execute("NOT VALID SQL"), DbError);
}

TEST_CASE("unknown named parameter throws", "[db][statement]") {
    Database db = Database::open_in_memory();
    db.execute("CREATE TABLE t (v INTEGER)");
    auto stmt = db.prepare("SELECT * FROM t WHERE v = :real");
    REQUIRE_THROWS_AS(stmt.bind(":missing", 1), DbError);
}

TEST_CASE("database is movable", "[db][database]") {
    Database a = Database::open_in_memory();
    a.execute("CREATE TABLE t (v INTEGER); INSERT INTO t VALUES (7)");
    Database b = std::move(a);

    auto stmt = b.prepare("SELECT v FROM t");
    REQUIRE(stmt.step());
    REQUIRE(stmt.column_int64(0) == 7);
}
