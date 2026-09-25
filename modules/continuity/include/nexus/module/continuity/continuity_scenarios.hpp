#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace nexus::db {
class Database;
}

namespace nexus::module::continuity {

enum class ScenarioKind { DiskFailure, ComputerTheft, RansomwareEvent, NewPcMigration };

[[nodiscard]] std::string_view scenario_name(ScenarioKind kind) noexcept;

/// One condition a scenario checks. An unready scenario's unmet checks are
/// its "gaps", shown to the user instead of a single opaque "not ready".
struct ScenarioCheck {
    std::string description;
    bool passed = false;
};

struct ScenarioStatus {
    ScenarioKind kind;
    /// True only when every check passed - never asserted from a single
    /// summary condition, always derived from `checks` so the two can't
    /// drift apart.
    bool ready = false;
    std::vector<ScenarioCheck> checks;
};

/// Evaluates all four fixed scenarios against real Backup/Continuity state.
/// Each check is a genuine, currently-true-or-false property (a job exists,
/// a snapshot was verified, retention spans enough time, a Capsule export
/// was recorded) - never a simulated or assumed outcome.
[[nodiscard]] std::vector<ScenarioStatus> evaluate_scenarios(nexus::db::Database& db);

} // namespace nexus::module::continuity
