#include "nexus/db/transaction.hpp"

#include "nexus/db/database.hpp"

namespace nexus::db {

Transaction::Transaction(Database& db) : db_(&db), lock_(db.sync()) {
    db_->execute("BEGIN");
}

Transaction::~Transaction() {
    if (!finished_) {
        try {
            db_->execute("ROLLBACK");
        } catch (...) {
            // A failed rollback in a destructor is not recoverable; swallow it.
        }
    }
}

void Transaction::commit() {
    db_->execute("COMMIT");
    finished_ = true;
}

} // namespace nexus::db
