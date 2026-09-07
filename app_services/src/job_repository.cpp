#include "nexus/services/job_repository.hpp"

#include <stdexcept>
#include <utility>

#include "nexus/db/database.hpp"
#include "nexus/db/statement.hpp"

#include "support.hpp"

namespace nexus::services {

using detail::bind_text_or_null;
using detail::column_time_or_null;

std::string_view to_string(JobState state) noexcept {
    switch (state) {
        case JobState::Pending:
            return "pending";
        case JobState::Running:
            return "running";
        case JobState::Succeeded:
            return "succeeded";
        case JobState::Failed:
            return "failed";
        case JobState::Cancelled:
            return "cancelled";
    }
    return "pending";
}

std::optional<JobState> job_state_from_string(std::string_view text) noexcept {
    if (text == "pending") {
        return JobState::Pending;
    }
    if (text == "running") {
        return JobState::Running;
    }
    if (text == "succeeded") {
        return JobState::Succeeded;
    }
    if (text == "failed") {
        return JobState::Failed;
    }
    if (text == "cancelled") {
        return JobState::Cancelled;
    }
    return std::nullopt;
}

bool is_terminal(JobState state) noexcept {
    return state == JobState::Succeeded || state == JobState::Failed ||
           state == JobState::Cancelled;
}

namespace {

JobRecord read_job(nexus::db::Statement& stmt) {
    JobRecord job;
    if (const auto id = nexus::core::Uuid::parse(stmt.column_text(0))) {
        job.id = *id;
    }
    job.module = stmt.column_text(1);
    job.kind = stmt.column_text(2);
    job.schedule = stmt.column_is_null(3) ? std::string{} : stmt.column_text(3);
    job.enabled = stmt.column_int64(4) != 0;
    job.config = stmt.column_is_null(5) ? std::string{} : stmt.column_text(5);
    if (const auto ts = nexus::core::from_iso8601(stmt.column_text(6))) {
        job.created_at = *ts;
    }
    if (const auto ts = nexus::core::from_iso8601(stmt.column_text(7))) {
        job.updated_at = *ts;
    }
    return job;
}

constexpr const char* kJobColumns =
    "id, module, kind, schedule, enabled, config, created_at, updated_at";

JobRunRecord read_run(nexus::db::Statement& stmt) {
    JobRunRecord run;
    if (const auto id = nexus::core::Uuid::parse(stmt.column_text(0))) {
        run.id = *id;
    }
    if (const auto id = nexus::core::Uuid::parse(stmt.column_text(1))) {
        run.job_id = *id;
    }
    run.state = job_state_from_string(stmt.column_text(2)).value_or(JobState::Pending);
    run.started_at = column_time_or_null(stmt, 3);
    run.finished_at = column_time_or_null(stmt, 4);
    run.progress = stmt.column_double(5);
    run.message = stmt.column_is_null(6) ? std::string{} : stmt.column_text(6);
    run.error = stmt.column_is_null(7) ? std::string{} : stmt.column_text(7);
    return run;
}

constexpr const char* kRunColumns =
    "id, job_id, state, started_at, finished_at, progress, message, error";

} // namespace

nexus::core::Uuid JobRepository::upsert_job(const JobRecord& job) {
    const nexus::core::Uuid id = job.id.is_nil() ? nexus::core::Uuid::generate() : job.id;
    const std::string now = nexus::core::to_iso8601(nexus::core::now());
    const std::string created =
        job.created_at.time_since_epoch().count() == 0
            ? now
            : nexus::core::to_iso8601(job.created_at);

    nexus::db::Statement stmt = db_->prepare(
        "INSERT INTO jobs (id, module, kind, schedule, enabled, config, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(id) DO UPDATE SET module = excluded.module, kind = excluded.kind, "
        "schedule = excluded.schedule, enabled = excluded.enabled, config = excluded.config, "
        "updated_at = excluded.updated_at");
    stmt.bind(1, id.to_string());
    stmt.bind(2, job.module);
    stmt.bind(3, job.kind);
    bind_text_or_null(stmt, 4, job.schedule);
    stmt.bind(5, job.enabled ? 1 : 0);
    bind_text_or_null(stmt, 6, job.config);
    stmt.bind(7, created);
    stmt.bind(8, now);
    stmt.step();
    return id;
}

std::optional<JobRecord> JobRepository::find_job(const nexus::core::Uuid& id) const {
    nexus::db::Statement stmt =
        db_->prepare(std::string("SELECT ") + kJobColumns + " FROM jobs WHERE id = ?");
    stmt.bind(1, id.to_string());
    if (!stmt.step()) {
        return std::nullopt;
    }
    return read_job(stmt);
}

std::vector<JobRecord> JobRepository::list_jobs() const {
    nexus::db::Statement stmt = db_->prepare(std::string("SELECT ") + kJobColumns +
                                             " FROM jobs ORDER BY module, kind, created_at");
    std::vector<JobRecord> jobs;
    while (stmt.step()) {
        jobs.push_back(read_job(stmt));
    }
    return jobs;
}

bool JobRepository::set_job_enabled(const nexus::core::Uuid& id, bool enabled) {
    nexus::db::Statement stmt =
        db_->prepare("UPDATE jobs SET enabled = ?, updated_at = ? WHERE id = ?");
    stmt.bind(1, enabled ? 1 : 0);
    stmt.bind(2, nexus::core::to_iso8601(nexus::core::now()));
    stmt.bind(3, id.to_string());
    stmt.step();
    return db_->changes() > 0;
}

bool JobRepository::remove_job(const nexus::core::Uuid& id) {
    nexus::db::Statement stmt = db_->prepare("DELETE FROM jobs WHERE id = ?");
    stmt.bind(1, id.to_string());
    stmt.step();
    return db_->changes() > 0;
}

nexus::core::Uuid JobRepository::start_run(const nexus::core::Uuid& job_id) {
    const nexus::core::Uuid run_id = nexus::core::Uuid::generate();
    nexus::db::Statement stmt = db_->prepare(
        "INSERT INTO job_runs (id, job_id, state, started_at, progress) VALUES (?, ?, ?, ?, 0)");
    stmt.bind(1, run_id.to_string());
    stmt.bind(2, job_id.to_string());
    stmt.bind(3, to_string(JobState::Running));
    stmt.bind(4, nexus::core::to_iso8601(nexus::core::now()));
    stmt.step();
    return run_id;
}

void JobRepository::update_run_progress(const nexus::core::Uuid& run_id, double fraction,
                                        std::string message) {
    nexus::db::Statement stmt =
        db_->prepare("UPDATE job_runs SET progress = ?, message = ? WHERE id = ?");
    stmt.bind(1, fraction < 0.0 ? 0.0 : (fraction > 1.0 ? 1.0 : fraction));
    bind_text_or_null(stmt, 2, message);
    stmt.bind(3, run_id.to_string());
    stmt.step();
}

void JobRepository::finish_run(const nexus::core::Uuid& run_id, JobState terminal,
                               std::string error) {
    if (!is_terminal(terminal)) {
        throw std::invalid_argument("finish_run requires a terminal state");
    }
    nexus::db::Statement stmt = db_->prepare(
        "UPDATE job_runs SET state = ?, finished_at = ?, error = ?, "
        "progress = CASE WHEN ? = 'succeeded' THEN 1.0 ELSE progress END WHERE id = ?");
    const std::string state = std::string(to_string(terminal));
    stmt.bind(1, state);
    stmt.bind(2, nexus::core::to_iso8601(nexus::core::now()));
    bind_text_or_null(stmt, 3, error);
    stmt.bind(4, state);
    stmt.bind(5, run_id.to_string());
    stmt.step();
}

std::vector<JobRunRecord> JobRepository::runs_for(const nexus::core::Uuid& job_id,
                                                  std::size_t limit) const {
    nexus::db::Statement stmt = db_->prepare(
        std::string("SELECT ") + kRunColumns +
        " FROM job_runs WHERE job_id = ? ORDER BY started_at DESC, rowid DESC LIMIT ?");
    stmt.bind(1, job_id.to_string());
    stmt.bind(2, static_cast<std::int64_t>(limit));
    std::vector<JobRunRecord> runs;
    while (stmt.step()) {
        runs.push_back(read_run(stmt));
    }
    return runs;
}

std::optional<JobRunRecord> JobRepository::latest_run(const nexus::core::Uuid& job_id) const {
    const auto runs = runs_for(job_id, 1);
    if (runs.empty()) {
        return std::nullopt;
    }
    return runs.front();
}

} // namespace nexus::services
