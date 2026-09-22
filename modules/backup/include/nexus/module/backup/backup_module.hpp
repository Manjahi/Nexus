#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "nexus/core/id.hpp"
#include "nexus/jobs/schedule_table.hpp"
#include "nexus/services/event_bus.hpp"
#include "nexus/services/module.hpp"
#include "nexus/services/report_center.hpp"

namespace nexus::module::backup {

class ScheduledBackup;
struct BackupJob;

inline constexpr const char* kBackupReportKind = "backup-report";

/// Parses a job schedule string ("every 6h", "every 30m", "every 1d"). Returns
/// nullopt for an empty or unrecognised value.
[[nodiscard]] std::optional<std::chrono::seconds> parse_schedule(std::string_view text);

/// Backup & Recovery module. Backups run on demand from the UI on the thread
/// pool; the module owns the schema and the backup report.
class BackupModule : public nexus::services::Module {
public:
    BackupModule();
    ~BackupModule() override;

    [[nodiscard]] std::string_view id() const override { return "backup"; }
    void apply_migrations(nexus::db::Database& db) override;
    void start(nexus::services::ServiceContext& ctx) override;
    void stop() override;

    /// Re-reads one job from the database and re-arms its schedule (cancelling
    /// any previous one first) - lets the UI activate a new/edited/newly-
    /// enabled job's schedule immediately instead of requiring a restart. A
    /// no-op if the module hasn't started yet.
    void reschedule_job(const nexus::core::Uuid& job_id);

    /// Bytes reclaimable per the most recent Storage duplicate scan (spec
    /// section 9 hook #1), or nullopt if none has been seen since this
    /// module started. Backed by an EventBus subscription, not a query -
    /// the UI reads this to warn before running a backup, so it's cheap.
    [[nodiscard]] std::optional<std::uint64_t> latest_known_duplicate_bytes() const noexcept;

private:
    void arm(const BackupJob& job);
    void disarm(const nexus::core::Uuid& job_id);

    nexus::services::ServiceContext* ctx_ = nullptr;
    nexus::services::ReportCenter::GeneratorId report_id_{};
    bool report_registered_ = false;

    std::map<nexus::core::Uuid, std::shared_ptr<ScheduledBackup>> scheduled_;
    std::map<nexus::core::Uuid, nexus::jobs::ScheduleTable::Id> schedule_ids_;

    nexus::services::EventBus::Token duplicates_token_{};
    bool subscribed_to_duplicates_ = false;
    // -1 means "none seen yet"; relaxed atomics are enough since this is a
    // single scalar with no ordering dependency on anything else.
    std::atomic<std::int64_t> latest_duplicate_bytes_{-1};

    // Spec section 9 hook #3: pause scheduled backups against a network
    // (UNC) destination during a Connectivity-detected total outage, instead
    // of letting every tick fail loudly. Defaults to true (assume reachable)
    // so a slow first ConnectivityModule tick never blocks a same-machine
    // local backup. Each ScheduledBackup gets a raw pointer to this - it
    // outlives every job's schedule since it's torn down in stop(), after
    // schedule_ids_ is cancelled and drained.
    nexus::services::EventBus::Token connectivity_token_{};
    bool subscribed_to_connectivity_ = false;
    std::atomic<bool> network_reachable_{true};
};

} // namespace nexus::module::backup
