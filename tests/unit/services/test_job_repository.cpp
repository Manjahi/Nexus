#include "nexus/services/job_repository.hpp"

#include "nexus/db/database.hpp"
#include "nexus/db/migration.hpp"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

using nexus::services::JobRecord;
using nexus::services::JobRepository;
using nexus::services::JobState;

namespace {
nexus::db::Database migrated_db() {
    auto db = nexus::db::Database::open_in_memory();
    nexus::db::migrate(db, nexus::db::core_migrations());
    return db;
}

JobRecord sample_job() {
    JobRecord job;
    job.module = "backup";
    job.kind = "incremental_snapshot";
    job.schedule = "every 6h";
    job.config = R"({"target":"D:/backups"})";
    return job;
}
} // namespace

TEST_CASE("upsert_job inserts then updates the same row", "[services][jobs]") {
    auto db = migrated_db();
    JobRepository repo(db);

    const auto id = repo.upsert_job(sample_job());
    REQUIRE_FALSE(id.is_nil());

    auto stored = repo.find_job(id);
    REQUIRE(stored.has_value());
    REQUIRE(stored->module == "backup");
    REQUIRE(stored->schedule == "every 6h");
    REQUIRE(stored->enabled);
    const auto created_first = stored->created_at;

    JobRecord edit = *stored;
    edit.schedule = "every 12h";
    edit.enabled = false;
    const auto id2 = repo.upsert_job(edit);
    REQUIRE(id2 == id);

    stored = repo.find_job(id);
    REQUIRE(stored->schedule == "every 12h");
    REQUIRE_FALSE(stored->enabled);
    REQUIRE(stored->created_at == created_first); // unchanged on update
    REQUIRE(repo.list_jobs().size() == 1);
}

TEST_CASE("set_job_enabled and remove_job report whether a row matched", "[services][jobs]") {
    auto db = migrated_db();
    JobRepository repo(db);
    const auto id = repo.upsert_job(sample_job());

    REQUIRE(repo.set_job_enabled(id, false));
    REQUIRE_FALSE(repo.find_job(id)->enabled);
    REQUIRE_FALSE(repo.set_job_enabled(nexus::core::Uuid::generate(), true));

    REQUIRE(repo.remove_job(id));
    REQUIRE_FALSE(repo.remove_job(id));
    REQUIRE(repo.list_jobs().empty());
}

TEST_CASE("run lifecycle: start, progress, finish", "[services][jobs]") {
    auto db = migrated_db();
    JobRepository repo(db);
    const auto job_id = repo.upsert_job(sample_job());

    const auto run_id = repo.start_run(job_id);
    auto run = repo.latest_run(job_id);
    REQUIRE(run.has_value());
    REQUIRE(run->id == run_id);
    REQUIRE(run->state == JobState::Running);
    REQUIRE(run->started_at.has_value());
    REQUIRE_FALSE(run->finished_at.has_value());

    repo.update_run_progress(run_id, 0.5, "halfway");
    run = repo.latest_run(job_id);
    REQUIRE(run->progress == 0.5);
    REQUIRE(run->message == "halfway");

    repo.finish_run(run_id, JobState::Succeeded);
    run = repo.latest_run(job_id);
    REQUIRE(run->state == JobState::Succeeded);
    REQUIRE(run->finished_at.has_value());
    REQUIRE(run->progress == 1.0); // succeeded snaps to complete
}

TEST_CASE("finish_run records failure detail and rejects non-terminal states",
          "[services][jobs]") {
    auto db = migrated_db();
    JobRepository repo(db);
    const auto job_id = repo.upsert_job(sample_job());
    const auto run_id = repo.start_run(job_id);
    repo.update_run_progress(run_id, 0.3);

    REQUIRE_THROWS_AS(repo.finish_run(run_id, JobState::Running), std::invalid_argument);

    repo.finish_run(run_id, JobState::Failed, "disk offline");
    const auto run = repo.latest_run(job_id);
    REQUIRE(run->state == JobState::Failed);
    REQUIRE(run->error == "disk offline");
    REQUIRE(run->progress == 0.3); // preserved on failure
}

TEST_CASE("removing a job cascades to its runs", "[services][jobs]") {
    auto db = migrated_db();
    JobRepository repo(db);
    const auto job_id = repo.upsert_job(sample_job());
    repo.start_run(job_id);
    repo.start_run(job_id);
    REQUIRE(repo.runs_for(job_id).size() == 2);

    REQUIRE(repo.remove_job(job_id));
    REQUIRE(repo.runs_for(job_id).empty());
}

TEST_CASE("job state strings round-trip", "[services][jobs]") {
    for (const JobState s : {JobState::Pending, JobState::Running, JobState::Succeeded,
                             JobState::Failed, JobState::Cancelled}) {
        REQUIRE(nexus::services::job_state_from_string(nexus::services::to_string(s)) == s);
    }
    REQUIRE_FALSE(nexus::services::job_state_from_string("bogus").has_value());
}
