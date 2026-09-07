#pragma once

namespace nexus::db {

class Database;

/// Scoped transaction. `BEGIN` on construction; the destructor issues
/// `ROLLBACK` unless `commit()` was called.
class Transaction {
public:
    explicit Transaction(Database& db);
    ~Transaction();

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    Transaction(Transaction&&) = delete;
    Transaction& operator=(Transaction&&) = delete;

    void commit();

private:
    Database* db_;
    bool finished_{false};
};

} // namespace nexus::db
