#pragma once

#include <string>

#include "nexus/services/report_center.hpp"

namespace nexus::db {
class Database;
}

namespace nexus::module::continuity {

inline constexpr const char* kRecoveryPlanKind = "continuity-recovery-plan";

/// Lists tracked assets (with coverage/verified status), the four recovery
/// scenarios (with their checklists), and rehearsal history - the same data
/// the Continuity Lab page itself shows, rendered through the shared
/// report_format.hpp theme so it looks like every other generated report.
[[nodiscard]] std::string render_recovery_plan(nexus::db::Database& db,
                                               nexus::services::ReportFormat format);

} // namespace nexus::module::continuity
