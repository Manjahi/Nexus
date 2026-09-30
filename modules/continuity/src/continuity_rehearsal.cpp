#include "nexus/module/continuity/continuity_rehearsal.hpp"

#include "nexus/module/backup/backup_engine.hpp"
#include "nexus/module/backup/backup_repository.hpp"
#include "nexus/module/backup/object_store.hpp"
#include "nexus/module/backup/restore_engine.hpp"
#include "nexus/module/continuity/continuity_repository.hpp"

#include <optional>

namespace nexus::module::continuity {

namespace {

struct PickedSnapshot {
    nexus::module::backup::BackupJob job;
    nexus::module::backup::SnapshotRecord snapshot;
};

std::optional<PickedSnapshot>
pick_most_recent_snapshot(nexus::module::backup::BackupRepository& backup) {
    std::optional<PickedSnapshot> best;
    for (const auto& job : backup.list_jobs()) {
        const auto snap = backup.latest_snapshot(job.id);
        if (!snap) {
            continue;
        }
        if (!best || snap->started_at > best->snapshot.started_at) {
            best = PickedSnapshot{job, *snap};
        }
    }
    return best;
}

} // namespace

RehearsalResult run_quick_rehearsal(nexus::db::Database& db,
                                    const std::filesystem::path& scratch_dir) {
    ContinuityRepository continuity(db);
    nexus::module::backup::BackupRepository backup(db);

    const auto id = continuity.begin_rehearsal("Quick Rehearsal");

    RehearsalResult result;
    result.rehearsal_id = id;

    const auto picked = pick_most_recent_snapshot(backup);
    if (!picked) {
        result.outcome = "failed";
        result.detail = "No backup snapshot exists to rehearse";
        continuity.finish_rehearsal(id, result.outcome, 0, 0, result.detail);
        return result;
    }

    nexus::module::backup::ObjectStore store(std::filesystem::path(picked->job.destination) /
                                             "objects");
    nexus::module::backup::RestoreEngine restorer(store, backup);
    const auto restore = restorer.restore(picked->snapshot.id, scratch_dir);

    nexus::module::backup::BackupEngine engine(store, &backup);
    const auto verify = engine.verify(picked->snapshot.id);
    if (verify.healthy()) {
        backup.mark_verified(picked->snapshot.id, nexus::core::now());
    }

    result.files_restored = restore.files_restored;
    result.bytes_restored = picked->snapshot.total_bytes;
    result.detail = "restored " + std::to_string(restore.files_restored) + " file(s), " +
                    std::to_string(restore.missing_blobs) + " missing; verified " +
                    std::to_string(verify.ok) + "/" + std::to_string(verify.checked) +
                    " blob(s) ok, " + std::to_string(verify.corrupt) + " corrupt, " +
                    std::to_string(verify.missing) + " missing";

    if (restore.ok() && verify.healthy()) {
        result.outcome = "success";
    } else if (restore.files_restored > 0 || verify.ok > 0) {
        result.outcome = "partial";
    } else {
        result.outcome = "failed";
    }

    continuity.finish_rehearsal(id, result.outcome, result.files_restored, result.bytes_restored,
                                result.detail);
    return result;
}

} // namespace nexus::module::continuity
