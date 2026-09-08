#pragma once

#include <mutex>

namespace nexus::db {

class Database;

/// Scoped transaction. Acquires the connection lock and issues `BEGIN` on
/// construction; the destructor issues `ROLLBACK` unless `commit()` was called.
/// The lock is held for the transaction's whole lifetime, so no other thread
/// can touch the connection mid-transaction.
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
    std::unique_lock<std::recursive_mutex> lock_;
    bool finished_{false};
};

} // namespace nexus::db
