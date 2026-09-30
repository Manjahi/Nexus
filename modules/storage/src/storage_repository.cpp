#include "nexus/module/storage/storage_repository.hpp"

#include "nexus/db/database.hpp"
#include "nexus/db/statement.hpp"
#include "nexus/db/transaction.hpp"

#include <string>

namespace nexus::module::storage {

namespace {

constexpr const char* kScanColumns =
    "id, root, started_at, finished_at, files_seen, bytes_seen, duplicate_groups, "
    "reclaimable_bytes, state";

ScanRecord read_scan(nexus::db::Statement& stmt) {
    ScanRecord record;
    if (const auto id = nexus::core::Uuid::parse(stmt.column_text(0))) {
        record.id = *id;
    }
    record.root = stmt.column_text(1);
    if (const auto at = nexus::core::from_iso8601(stmt.column_text(2))) {
        record.started_at = *at;
    }
    if (!stmt.column_is_null(3)) {
        record.finished_at = nexus::core::from_iso8601(stmt.column_text(3));
    }
    record.files_seen = static_cast<std::uint64_t>(stmt.column_int64(4));
    record.bytes_seen = static_cast<std::uint64_t>(stmt.column_int64(5));
    record.duplicate_groups = static_cast<int>(stmt.column_int64(6));
    record.reclaimable_bytes = static_cast<std::uint64_t>(stmt.column_int64(7));
    record.state = stmt.column_text(8);
    return record;
}

} // namespace

nexus::core::Uuid StorageRepository::begin_scan(std::string_view root) {
    const nexus::core::Uuid id = nexus::core::Uuid::generate();
    nexus::db::Statement stmt = db_->prepare(
        "INSERT INTO file_scans (id, root, started_at, state) VALUES (?, ?, ?, 'running')");
    stmt.bind(1, id.to_string());
    stmt.bind(2, root);
    stmt.bind(3, nexus::core::to_iso8601(nexus::core::now()));
    stmt.step();
    return id;
}

void StorageRepository::finish_scan(const nexus::core::Uuid& scan_id, const ScanSummary& summary,
                                    std::string_view state) {
    nexus::db::Statement stmt =
        db_->prepare("UPDATE file_scans SET finished_at = ?, files_seen = ?, bytes_seen = ?, "
                     "duplicate_groups = ?, reclaimable_bytes = ?, state = ? WHERE id = ?");
    stmt.bind(1, nexus::core::to_iso8601(nexus::core::now()));
    stmt.bind(2, static_cast<std::int64_t>(summary.files_seen));
    stmt.bind(3, static_cast<std::int64_t>(summary.bytes_seen));
    stmt.bind(4, static_cast<std::int64_t>(summary.groups.size()));
    stmt.bind(5, static_cast<std::int64_t>(summary.reclaimable_bytes()));
    stmt.bind(6, state);
    stmt.bind(7, scan_id.to_string());
    stmt.step();
}

std::int64_t StorageRepository::add_group(const nexus::core::Uuid& scan_id,
                                          const DuplicateGroup& group) {
    nexus::db::Transaction tx(*db_);

    nexus::db::Statement insert_group = db_->prepare(
        "INSERT INTO duplicate_groups (scan_id, digest, file_size, file_count, reclaimable_bytes) "
        "VALUES (?, ?, ?, ?, ?)");
    insert_group.bind(1, scan_id.to_string());
    insert_group.bind(2, group.digest);
    insert_group.bind(3, static_cast<std::int64_t>(group.file_size));
    insert_group.bind(4, static_cast<std::int64_t>(group.files.size()));
    insert_group.bind(5, static_cast<std::int64_t>(group.reclaimable_bytes()));
    insert_group.step();
    const std::int64_t group_id = insert_group.last_insert_rowid();

    nexus::db::Statement insert_file = db_->prepare(
        "INSERT INTO scanned_files (scan_id, path, size, digest, group_id) VALUES (?, ?, ?, ?, ?)");
    for (const std::filesystem::path& file : group.files) {
        insert_file.bind(1, scan_id.to_string());
        insert_file.bind(2, file.generic_string());
        insert_file.bind(3, static_cast<std::int64_t>(group.file_size));
        insert_file.bind(4, group.digest);
        insert_file.bind(5, group_id);
        insert_file.step();
        insert_file.reset();
    }

    tx.commit();
    return group_id;
}

std::vector<ScanRecord> StorageRepository::scans(std::size_t limit) const {
    nexus::db::Statement stmt =
        db_->prepare(std::string("SELECT ") + kScanColumns +
                     " FROM file_scans ORDER BY started_at DESC, rowid DESC "
                     "LIMIT ?");
    stmt.bind(1, static_cast<std::int64_t>(limit));
    std::vector<ScanRecord> out;
    while (stmt.step()) {
        out.push_back(read_scan(stmt));
    }
    return out;
}

std::optional<ScanRecord> StorageRepository::latest_scan() const {
    const auto all = scans(1);
    if (all.empty()) {
        return std::nullopt;
    }
    return all.front();
}

std::vector<GroupRecord> StorageRepository::groups_for(const nexus::core::Uuid& scan_id) const {
    nexus::db::Statement stmt = db_->prepare(
        "SELECT id, digest, file_size, file_count, reclaimable_bytes FROM duplicate_groups "
        "WHERE scan_id = ? ORDER BY reclaimable_bytes DESC, id");
    stmt.bind(1, scan_id.to_string());
    std::vector<GroupRecord> out;
    while (stmt.step()) {
        GroupRecord g;
        g.id = stmt.column_int64(0);
        g.digest = stmt.column_text(1);
        g.file_size = static_cast<std::uint64_t>(stmt.column_int64(2));
        g.file_count = static_cast<int>(stmt.column_int64(3));
        g.reclaimable_bytes = static_cast<std::uint64_t>(stmt.column_int64(4));
        out.push_back(std::move(g));
    }
    return out;
}

std::vector<std::string> StorageRepository::files_in_group(std::int64_t group_id) const {
    nexus::db::Statement stmt =
        db_->prepare("SELECT path FROM scanned_files WHERE group_id = ? ORDER BY path");
    stmt.bind(1, group_id);
    std::vector<std::string> out;
    while (stmt.step()) {
        out.push_back(stmt.column_text(0));
    }
    return out;
}

std::int64_t StorageRepository::prune_scans_keeping(std::size_t keep) {
    nexus::db::Statement stmt =
        db_->prepare("DELETE FROM file_scans WHERE id NOT IN "
                     "(SELECT id FROM file_scans ORDER BY started_at DESC, rowid DESC LIMIT ?)");
    stmt.bind(1, static_cast<std::int64_t>(keep));
    stmt.step();
    return stmt.changes();
}

} // namespace nexus::module::storage
