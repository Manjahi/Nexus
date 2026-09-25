#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "nexus/core/id.hpp"

namespace nexus::db {
class Database;
}

namespace nexus::module::continuity {

/// One tracked asset's coverage, as of the last compute_readiness() call.
struct AssetReadiness {
    nexus::core::Uuid asset_id;
    std::string label;
    /// True if some backup job's source tree covers this asset's path (or,
    /// for a Credential asset, if a Recovery Capsule export exists).
    bool covered = false;
    /// True if the covering snapshot has been verified (BackupEngine::
    /// verify() found it intact) - or, for Credential, same as `covered`
    /// (a Vault export has no separate verify step to distinguish).
    bool verified = false;
};

/// Overall disaster-recovery readiness, computed fresh from current Backup/
/// Continuity state - no background job, this is cheap query-time work.
struct ReadinessReport {
    /// 0-100. 70% weight on coverage (is it backed up at all?), 30% on
    /// verification (do we actually know that backup is intact?) - coverage
    /// matters more because an uncovered asset is a certain loss, while an
    /// unverified-but-covered one is only a risk. 0 when nothing is tracked
    /// yet, which callers should render as "not yet reviewed", not "0%
    /// ready" - those mean different things to a user.
    int score = 0;
    int tracked_count = 0;
    int covered_count = 0;
    int verified_count = 0;
    std::vector<AssetReadiness> assets;
    /// Tracked-and-covered bytes divided by a measured throughput - see
    /// compute_readiness()'s kAssumedThroughputBytesPerSec. Unset when
    /// nothing is covered yet.
    std::optional<std::chrono::seconds> estimated_rebuild_time;
};

[[nodiscard]] ReadinessReport compute_readiness(nexus::db::Database& db);

} // namespace nexus::module::continuity
