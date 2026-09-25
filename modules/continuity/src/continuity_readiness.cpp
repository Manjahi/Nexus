#include "nexus/module/continuity/continuity_readiness.hpp"

#include "nexus/module/backup/backup_repository.hpp"
#include "nexus/module/continuity/continuity_repository.hpp"

#include <cmath>
#include <filesystem>
#include <unordered_set>

namespace nexus::module::continuity {

namespace {

// "Backup run (cold)" from docs/PERFORMANCE.md - the closest measured
// throughput to "how fast can this machine move backed-up bytes", since
// RestoreEngine has no benchmark of its own yet (a documented gap there,
// not an oversight here). A restore is a plain copy with no BLAKE3 hashing,
// so backup's write throughput is typically a pessimistic (safe-side)
// stand-in for it, not an optimistic one.
constexpr double kAssumedThroughputBytesPerSec = 2.3 * 1024.0 * 1024.0;

bool path_covered_by_job(const std::string& asset_path, const std::string& job_source_root) {
    if (asset_path.empty()) {
        return false;
    }
    std::error_code ec;
    const auto relative =
        std::filesystem::relative(asset_path, job_source_root, ec).generic_string();
    // relative() returns a "../"-leading path (or fails with ec) when
    // asset_path isn't under job_source_root at all.
    return !ec && !relative.empty() && relative.rfind("..", 0) != 0;
}

} // namespace

ReadinessReport compute_readiness(nexus::db::Database& db) {
    ContinuityRepository continuity(db);
    nexus::module::backup::BackupRepository backup(db);

    const auto assets = continuity.list_assets();
    const auto jobs = backup.list_jobs();
    const auto capsule_exported = continuity.latest_capsule_export().has_value();

    ReadinessReport report;
    report.tracked_count = static_cast<int>(assets.size());

    std::unordered_set<std::string> counted_jobs;
    std::uint64_t covered_bytes = 0;

    for (const auto& asset : assets) {
        AssetReadiness ar;
        ar.asset_id = asset.id;
        ar.label = asset.label;

        if (asset.kind == AssetKind::Credential) {
            ar.covered = capsule_exported;
            ar.verified = capsule_exported;
        } else {
            for (const auto& job : jobs) {
                if (!path_covered_by_job(asset.path, job.source_root)) {
                    continue;
                }
                const auto snap = backup.latest_snapshot(job.id);
                if (!snap) {
                    continue;
                }
                ar.covered = true;
                ar.verified = snap->verified_at.has_value();
                if (counted_jobs.insert(job.id.to_string()).second) {
                    covered_bytes += snap->total_bytes;
                }
                break; // first covering job with a snapshot is enough
            }
        }

        if (ar.covered) {
            ++report.covered_count;
        }
        if (ar.verified) {
            ++report.verified_count;
        }
        report.assets.push_back(ar);
    }

    if (report.tracked_count > 0) {
        const double coverage_fraction =
            static_cast<double>(report.covered_count) / static_cast<double>(report.tracked_count);
        const double verified_fraction =
            static_cast<double>(report.verified_count) / static_cast<double>(report.tracked_count);
        report.score = static_cast<int>(
            std::lround(100.0 * (0.7 * coverage_fraction + 0.3 * verified_fraction)));
    }

    if (covered_bytes > 0) {
        report.estimated_rebuild_time = std::chrono::seconds(static_cast<long long>(
            static_cast<double>(covered_bytes) / kAssumedThroughputBytesPerSec));
    }

    return report;
}

} // namespace nexus::module::continuity
