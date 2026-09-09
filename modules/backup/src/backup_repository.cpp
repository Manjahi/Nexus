#include "nexus/module/backup/backup_repository.hpp"

#include <string>

#include "nexus/db/database.hpp"
#include "nexus/db/statement.hpp"
#include "nexus/db/transaction.hpp"

namespace nexus::module::backup {

namespace {

constexpr const char* kJobColumns =
    "id, name, source_root, destination, schedule, retention_keep, enabled, exclusions, created_at";

BackupJob read_job(nexus::db::Statement& stmt) {
    BackupJob job;
    if (const auto id = nexus::core::Uuid::parse(stmt.column_text(0))) {
        job.id = *id;
    }
    job.name = stmt.column_text(1);
    job.source_root = stmt.column_text(2);
    job.destination = stmt.column_text(3);
    job.schedule = stmt.column_is_null(4) ? std::string{} : stmt.column_text(4);
    job.retention_keep = static_cast<int>(stmt.column_int64(5));
    job.enabled = stmt.column_int64(6) != 0;
    job.exclusions = stmt.column_text(7);
    if (const auto at = nexus::core::from_iso8601(stmt.column_text(8))) {
        job.created_at = *at;
    }
    return job;
}

constexpr const char* kSnapshotColumns =
    "id, backup_job_id, started_at, finished_at, state, file_count, total_bytes, new_bytes";

SnapshotRecord read_snapshot(nexus::db::Statement& stmt) {
    SnapshotRecord snap;
    if (const auto id = nexus::core::Uuid::parse(stmt.column_text(0))) {
        snap.id = *id;
    }
    if (const auto id = nexus::core::Uuid::parse(stmt.column_text(1))) {
        snap.backup_job_id = *id;
    }
    if (const auto at = nexus::core::from_iso8601(stmt.column_text(2))) {
        snap.started_at = *at;
    }
    if (!stmt.column_is_null(3)) {
        snap.finished_at = nexus::core::from_iso8601(stmt.column_text(3));
    }
    snap.state = stmt.column_text(4);
    snap.file_count = static_cast<std::uint64_t>(stmt.column_int64(5));
    snap.total_bytes = static_cast<std::uint64_t>(stmt.column_int64(6));
    snap.new_bytes = static_cast<std::uint64_t>(stmt.column_int64(7));
    return snap;
}

} // namespace

nexus::core::Uuid BackupRepository::upsert_job(const BackupJob& job) {
    const nexus::core::Uuid id = job.id.is_nil() ? nexus::core::Uuid::generate() : job.id;
    const std::string now = nexus::core::to_iso8601(nexus::core::now());
    const std::string created = job.created_at.time_since_epoch().count() == 0
                                    ? now
                                    : nexus::core::to_iso8601(job.created_at);

    nexus::db::Statement stmt = db_->prepare(
        "INSERT INTO backup_jobs (id, name, source_root, destination, schedule, retention_keep, "
        "enabled, exclusions, created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(id) DO UPDATE SET name = excluded.name, source_root = excluded.source_root, "
        "destination = excluded.destination, schedule = excluded.schedule, "
        "retention_keep = excluded.retention_keep, enabled = excluded.enabled, "
        "exclusions = excluded.exclusions");
    stmt.bind(1, id.to_string());
    stmt.bind(2, job.name);
    stmt.bind(3, job.source_root);
    stmt.bind(4, job.destination);
    if (job.schedule.empty()) {
        stmt.bind(5, nullptr);
    } else {
        stmt.bind(5, job.schedule);
    }
    stmt.bind(6, static_cast<std::int64_t>(job.retention_keep));
    stmt.bind(7, job.enabled ? 1 : 0);
    stmt.bind(8, job.exclusions);
    stmt.bind(9, created);
    stmt.step();
    return id;
}

std::optional<BackupJob> BackupRepository::find_job(const nexus::core::Uuid& id) const {
    nexus::db::Statement stmt =
        db_->prepare(std::string("SELECT ") + kJobColumns + " FROM backup_jobs WHERE id = ?");
    stmt.bind(1, id.to_string());
    if (!stmt.step()) {
        return std::nullopt;
    }
    return read_job(stmt);
}

std::vector<BackupJob> BackupRepository::list_jobs() const {
    nexus::db::Statement stmt = db_->prepare(std::string("SELECT ") + kJobColumns +
                                             " FROM backup_jobs ORDER BY name, created_at");
    std::vector<BackupJob> jobs;
    while (stmt.step()) {
        jobs.push_back(read_job(stmt));
    }
    return jobs;
}

bool BackupRepository::remove_job(const nexus::core::Uuid& id) {
    nexus::db::Statement stmt = db_->prepare("DELETE FROM backup_jobs WHERE id = ?");
    stmt.bind(1, id.to_string());
    stmt.step();
    return stmt.changes() > 0;
}

nexus::core::Uuid BackupRepository::begin_snapshot(const nexus::core::Uuid& job_id) {
    const nexus::core::Uuid id = nexus::core::Uuid::generate();
    nexus::db::Statement stmt = db_->prepare(
        "INSERT INTO snapshots (id, backup_job_id, started_at, state) VALUES (?, ?, ?, 'running')");
    stmt.bind(1, id.to_string());
    stmt.bind(2, job_id.to_string());
    stmt.bind(3, nexus::core::to_iso8601(nexus::core::now()));
    stmt.step();
    return id;
}

void BackupRepository::add_snapshot_files(const nexus::core::Uuid& snapshot_id,
                                          std::span<const SnapshotFile> files) {
    if (files.empty()) {
        return;
    }
    nexus::db::Transaction tx(*db_);
    nexus::db::Statement stmt = db_->prepare(
        "INSERT INTO snapshot_files (snapshot_id, path, size, mtime, digest) VALUES (?, ?, ?, ?, ?)");
    for (const SnapshotFile& file : files) {
        stmt.bind(1, snapshot_id.to_string());
        stmt.bind(2, file.path);
        stmt.bind(3, static_cast<std::int64_t>(file.size));
        if (file.mtime.empty()) {
            stmt.bind(4, nullptr);
        } else {
            stmt.bind(4, file.mtime);
        }
        stmt.bind(5, file.digest);
        stmt.step();
        stmt.reset();
    }
    tx.commit();
}

void BackupRepository::finish_snapshot(const nexus::core::Uuid& snapshot_id, std::string_view state,
                                       std::uint64_t file_count, std::uint64_t total_bytes,
                                       std::uint64_t new_bytes) {
    nexus::db::Statement stmt = db_->prepare(
        "UPDATE snapshots SET finished_at = ?, state = ?, file_count = ?, total_bytes = ?, "
        "new_bytes = ? WHERE id = ?");
    stmt.bind(1, nexus::core::to_iso8601(nexus::core::now()));
    stmt.bind(2, state);
    stmt.bind(3, static_cast<std::int64_t>(file_count));
    stmt.bind(4, static_cast<std::int64_t>(total_bytes));
    stmt.bind(5, static_cast<std::int64_t>(new_bytes));
    stmt.bind(6, snapshot_id.to_string());
    stmt.step();
}

std::vector<SnapshotRecord> BackupRepository::snapshots_for(const nexus::core::Uuid& job_id,
                                                            std::size_t limit) const {
    nexus::db::Statement stmt = db_->prepare(
        std::string("SELECT ") + kSnapshotColumns +
        " FROM snapshots WHERE backup_job_id = ? ORDER BY started_at DESC, rowid DESC LIMIT ?");
    stmt.bind(1, job_id.to_string());
    stmt.bind(2, static_cast<std::int64_t>(limit));
    std::vector<SnapshotRecord> out;
    while (stmt.step()) {
        out.push_back(read_snapshot(stmt));
    }
    return out;
}

std::optional<SnapshotRecord>
BackupRepository::latest_snapshot(const nexus::core::Uuid& job_id) const {
    const auto all = snapshots_for(job_id, 1);
    if (all.empty()) {
        return std::nullopt;
    }
    return all.front();
}

std::vector<SnapshotFile> BackupRepository::files_in(const nexus::core::Uuid& snapshot_id) const {
    nexus::db::Statement stmt = db_->prepare(
        "SELECT path, size, mtime, digest FROM snapshot_files WHERE snapshot_id = ? ORDER BY path");
    stmt.bind(1, snapshot_id.to_string());
    std::vector<SnapshotFile> out;
    while (stmt.step()) {
        SnapshotFile file;
        file.path = stmt.column_text(0);
        file.size = static_cast<std::uint64_t>(stmt.column_int64(1));
        file.mtime = stmt.column_is_null(2) ? std::string{} : stmt.column_text(2);
        file.digest = stmt.column_text(3);
        out.push_back(std::move(file));
    }
    return out;
}

std::vector<nexus::core::Uuid> BackupRepository::prune_snapshots(const nexus::core::Uuid& job_id,
                                                                 std::size_t keep) {
    nexus::db::Statement select = db_->prepare(
        "SELECT id FROM snapshots WHERE backup_job_id = ? AND state = 'completed' "
        "ORDER BY started_at DESC, rowid DESC");
    select.bind(1, job_id.to_string());

    std::vector<nexus::core::Uuid> to_remove;
    std::size_t seen = 0;
    while (select.step()) {
        ++seen;
        if (seen > keep) {
            if (const auto id = nexus::core::Uuid::parse(select.column_text(0))) {
                to_remove.push_back(*id);
            }
        }
    }

    nexus::db::Transaction tx(*db_);
    nexus::db::Statement del = db_->prepare("DELETE FROM snapshots WHERE id = ?");
    for (const auto& id : to_remove) {
        del.bind(1, id.to_string());
        del.step();
        del.reset();
    }
    tx.commit();
    return to_remove;
}

nexus::core::Uuid BackupRepository::record_restore(const nexus::core::Uuid& snapshot_id,
                                                   std::string_view target_dir,
                                                   std::string_view state,
                                                   std::uint64_t files_restored) {
    const nexus::core::Uuid id = nexus::core::Uuid::generate();
    nexus::db::Statement stmt = db_->prepare(
        "INSERT INTO restore_jobs (id, snapshot_id, target_dir, started_at, finished_at, state, "
        "files_restored) VALUES (?, ?, ?, ?, ?, ?, ?)");
    const std::string now = nexus::core::to_iso8601(nexus::core::now());
    stmt.bind(1, id.to_string());
    stmt.bind(2, snapshot_id.to_string());
    stmt.bind(3, target_dir);
    stmt.bind(4, now);
    stmt.bind(5, now);
    stmt.bind(6, state);
    stmt.bind(7, static_cast<std::int64_t>(files_restored));
    stmt.step();
    return id;
}

} // namespace nexus::module::backup
