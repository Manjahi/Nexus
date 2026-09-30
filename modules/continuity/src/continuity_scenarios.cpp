#include "nexus/module/continuity/continuity_scenarios.hpp"

#include "nexus/module/backup/backup_repository.hpp"
#include "nexus/module/continuity/continuity_readiness.hpp"
#include "nexus/module/continuity/continuity_repository.hpp"

#include <chrono>

namespace nexus::module::continuity {

std::string_view scenario_name(ScenarioKind kind) noexcept {
    switch (kind) {
        case ScenarioKind::DiskFailure:
            return "Disk Failure";
        case ScenarioKind::ComputerTheft:
            return "Computer Theft";
        case ScenarioKind::RansomwareEvent:
            return "Ransomware Event";
        case ScenarioKind::NewPcMigration:
            return "New-PC Migration";
    }
    return "Unknown";
}

namespace {

// How far apart the oldest and newest retained snapshot of some job must be
// for that job's history to count as "predates a hypothetical infection" -
// one calendar day is a conservative floor (real ransomware dwell time is
// often much longer), not a claim about any specific threat.
constexpr std::chrono::hours kRansomwareMinHistorySpan{24};

bool any_job_has_verified_latest_snapshot(nexus::module::backup::BackupRepository& backup) {
    for (const auto& job : backup.list_jobs()) {
        const auto snap = backup.latest_snapshot(job.id);
        if (snap && snap->verified_at.has_value()) {
            return true;
        }
    }
    return false;
}

bool any_job_spans_enough_history(nexus::module::backup::BackupRepository& backup) {
    for (const auto& job : backup.list_jobs()) {
        const auto snaps =
            backup.snapshots_for(job.id, static_cast<std::size_t>(job.retention_keep));
        if (snaps.size() < 2) {
            continue;
        }
        // snapshots_for() returns newest-first.
        const auto newest = snaps.front().started_at;
        const auto oldest = snaps.back().started_at;
        if (newest - oldest >= kRansomwareMinHistorySpan) {
            return true;
        }
    }
    return false;
}

ScenarioStatus finalize(ScenarioKind kind, std::vector<ScenarioCheck> checks) {
    ScenarioStatus status;
    status.kind = kind;
    status.ready = true;
    for (const auto& check : checks) {
        status.ready = status.ready && check.passed;
    }
    status.checks = std::move(checks);
    return status;
}

} // namespace

std::vector<ScenarioStatus> evaluate_scenarios(nexus::db::Database& db) {
    nexus::module::backup::BackupRepository backup(db);
    ContinuityRepository continuity(db);
    const auto readiness = compute_readiness(db);
    const bool has_jobs = !backup.list_jobs().empty();
    const bool all_tracked_covered =
        readiness.tracked_count > 0 && readiness.covered_count == readiness.tracked_count;
    const bool has_capsule_export = continuity.latest_capsule_export().has_value();

    std::vector<ScenarioStatus> scenarios;

    scenarios.push_back(finalize(ScenarioKind::DiskFailure,
                                 {
                                     {"A backup job is configured", has_jobs},
                                     {"The latest snapshot of at least one job has been verified",
                                      any_job_has_verified_latest_snapshot(backup)},
                                 }));

    scenarios.push_back(
        finalize(ScenarioKind::ComputerTheft,
                 {
                     {"A Recovery Capsule export exists", has_capsule_export},
                     {"Every tracked asset is covered by a backup", all_tracked_covered},
                 }));

    scenarios.push_back(
        finalize(ScenarioKind::RansomwareEvent,
                 {
                     {"Retention keeps a snapshot old enough to predate a hypothetical infection",
                      any_job_spans_enough_history(backup)},
                 }));

    scenarios.push_back(
        finalize(ScenarioKind::NewPcMigration,
                 {
                     {"Every tracked asset is covered by a backup", all_tracked_covered},
                     {"A Recovery Capsule export exists", has_capsule_export},
                 }));

    return scenarios;
}

} // namespace nexus::module::continuity
