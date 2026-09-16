#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "nexus/core/id.hpp"
#include "nexus/core/time.hpp"

namespace nexus::db {
class Database;
}

namespace nexus::services {

enum class JobState {
    Pending,
    Running,
    Succeeded,
    Failed,
    Cancelled,
};

[[nodiscard]] std::string_view to_string(JobState state) noexcept;
[[nodiscard]] std::optional<JobState> job_state_from_string(std::string_view text) noexcept;
[[nodiscard]] bool is_terminal(JobState state) noexcept;

/// A scheduled unit of work owned by a module. Mirrors the `jobs` table.
struct JobRecord {
    nexus::core::Uuid id;
    std::string module;
    std::string kind;
    std::string schedule; ///< optional human/cron-ish description
    bool enabled = true;
    std::string config; ///< optional opaque blob (JSON)
    nexus::core::Timestamp created_at{};
    nexus::core::Timestamp updated_at{};
};

/// One execution of a job. Mirrors the `job_runs` table.
struct JobRunRecord {
    nexus::core::Uuid id;
    nexus::core::Uuid job_id;
    JobState state = JobState::Pending;
    std::optional<nexus::core::Timestamp> started_at;
    std::optional<nexus::core::Timestamp> finished_at;
    double progress = 0.0;
    std::string message;
    std::string error;
};

/// CRUD over `jobs` / `job_runs`. Schedules and run history survive restarts
/// (UFR-014). Requires the core schema migration.
class JobRepository {
public:
    explicit JobRepository(nexus::db::Database& db) noexcept : db_(&db) {}

    /// Inserts a new job or updates the existing row with the same id. If
    /// `job.id` is nil a fresh id is generated. Returns the effective id.
    nexus::core::Uuid upsert_job(const JobRecord& job);

    [[nodiscard]] std::optional<JobRecord> find_job(const nexus::core::Uuid& id) const;
    [[nodiscard]] std::vector<JobRecord> list_jobs() const;
    bool set_job_enabled(const nexus::core::Uuid& id, bool enabled);
    bool remove_job(const nexus::core::Uuid& id); ///< cascades to its runs

    /// Opens a run in the Running state with started_at = now.
    nexus::core::Uuid start_run(const nexus::core::Uuid& job_id);
    void update_run_progress(const nexus::core::Uuid& run_id, double fraction,
                             std::string message = {});
    /// Closes a run. `terminal` must be a terminal state; finished_at = now.
    void finish_run(const nexus::core::Uuid& run_id, JobState terminal, std::string error = {});

    [[nodiscard]] std::vector<JobRunRecord> runs_for(const nexus::core::Uuid& job_id,
                                                     std::size_t limit = 50) const;
    [[nodiscard]] std::optional<JobRunRecord> latest_run(const nexus::core::Uuid& job_id) const;

    /// Deletes runs that finished before `cutoff` (never touches a run still
    /// Pending/Running - finished_at IS NULL for those). Returns rows removed.
    std::int64_t prune_finished_runs_before(nexus::core::Timestamp cutoff);

private:
    nexus::db::Database* db_;
};

} // namespace nexus::services
