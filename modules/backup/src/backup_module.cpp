#include "nexus/module/backup/backup_module.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <unordered_set>

#include "nexus/core/time.hpp"
#include "nexus/db/migration.hpp"
#include "nexus/fs/exclusion_rules.hpp"
#include "nexus/jobs/scheduler.hpp"
#include "nexus/module/backup/backup_engine.hpp"
#include "nexus/module/backup/backup_repository.hpp"
#include "nexus/module/backup/object_store.hpp"
#include "nexus/notify/notification_center.hpp"
#include "nexus/notify/severity.hpp"
#include "nexus/services/events/events.hpp"
#include "nexus/services/report_center.hpp"
#include "nexus/services/report_format.hpp"
#include "nexus/services/service_context.hpp"

namespace nexus::module::backup {

namespace {

using nexus::services::ReportFormat;

std::string mib(std::uint64_t bytes) {
    return nexus::services::report::number(static_cast<double>(bytes) / (1024.0 * 1024.0), 1);
}

bool is_unc_destination(const std::string& destination) {
    return destination.rfind("\\\\", 0) == 0;
}

std::string render(BackupRepository& repo, ReportFormat format) {
    const auto jobs = repo.list_jobs();
    if (format == ReportFormat::Csv) {
        std::string out = "job,source,last_snapshot,state,files,total_mib,new_mib\n";
        for (const auto& job : jobs) {
            const auto snap = repo.latest_snapshot(job.id);
            out += nexus::services::report::csv_cell(job.name.empty() ? job.source_root : job.name) +
                   "," + nexus::services::report::csv_cell(job.source_root) + "," +
                   (snap ? nexus::core::to_iso8601(snap->started_at) : std::string("never")) + "," +
                   (snap ? snap->state : std::string("-")) + "," +
                   (snap ? std::to_string(snap->file_count) : std::string("0")) + "," +
                   (snap ? mib(snap->total_bytes) : std::string("0")) + "," +
                   (snap ? mib(snap->new_bytes) : std::string("0")) + "\n";
        }
        return out;
    }

    std::string body =
        "<table><tr><th>Job</th><th>Source</th><th>Last snapshot</th><th>State</th><th>Files</th>"
        "<th>Total (MiB)</th><th>New (MiB)</th></tr>";
    for (const auto& job : jobs) {
        const auto snap = repo.latest_snapshot(job.id);
        const std::string state = snap ? snap->state : std::string("-");
        body += "<tr><td>" +
               nexus::services::report::html_escape(job.name.empty() ? job.source_root : job.name) +
               "</td><td>" + nexus::services::report::html_escape(job.source_root) + "</td><td>" +
               (snap ? nexus::core::to_iso8601(snap->started_at) : std::string("never")) +
               "</td><td>" +
               (snap ? "<span class=\"" + std::string(nexus::services::report::status_class(state)) +
                          "\">" + nexus::services::report::html_escape(state) + "</span>"
                    : "<span class=\"status-info\">-</span>") +
               "</td><td>" + (snap ? std::to_string(snap->file_count) : std::string("0")) +
               "</td><td>" + (snap ? mib(snap->total_bytes) : std::string("0")) + "</td><td>" +
               (snap ? mib(snap->new_bytes) : std::string("0")) + "</td></tr>";
    }
    body += "</table>";
    return nexus::services::report::html_document("Backup report", body);
}

} // namespace

std::optional<std::chrono::seconds> parse_schedule(std::string_view text) {
    std::string s;
    for (const char c : text) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            s += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    constexpr std::string_view prefix = "every";
    if (s.rfind(prefix, 0) == 0) {
        s.erase(0, prefix.size());
    }
    if (s.empty()) {
        return std::nullopt;
    }
    const char unit = s.back();
    s.pop_back();
    if (s.empty()) {
        return std::nullopt;
    }

    char* end = nullptr;
    const long value = std::strtol(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != '\0' || value <= 0) {
        return std::nullopt;
    }

    switch (unit) {
        case 's':
            return std::chrono::seconds{value};
        case 'm':
            return std::chrono::minutes{value};
        case 'h':
            return std::chrono::hours{value};
        case 'd':
            return std::chrono::hours{value * 24};
        default:
            return std::nullopt;
    }
}

/// Owns one job's backup run so an in-flight tick survives module stop.
class ScheduledBackup {
public:
    ScheduledBackup(nexus::db::Database& db, nexus::core::Uuid job_id,
                    nexus::notify::NotificationCenter& notifications,
                    const std::atomic<bool>* network_reachable = nullptr)
        : db_(&db),
          job_id_(job_id),
          notifications_(&notifications),
          network_reachable_(network_reachable) {}

    void set_active(bool active) noexcept { active_.store(active, std::memory_order_relaxed); }

    void tick() {
        if (!active_.load(std::memory_order_relaxed)) {
            return;
        }
        BackupRepository repo(*db_);
        const auto job = repo.find_job(job_id_);
        if (!job || !job->enabled) {
            return;
        }

        if (is_unc_destination(job->destination) && network_reachable_ != nullptr &&
            !network_reachable_->load(std::memory_order_relaxed)) {
            const std::string label = job->name.empty() ? job->source_root : job->name;
            if (!paused_notified_) {
                notifications_->post(
                    "backup", nexus::notify::Severity::Warning, "Backup paused: " + label,
                    "Network destination unreachable during a connectivity outage - will "
                    "resume automatically once it's back.");
                paused_notified_ = true;
            }
            return;
        }
        paused_notified_ = false;

        ObjectStore store(std::filesystem::path(job->destination) / "objects");
        BackupEngine engine(store, &repo);
        const auto rules = nexus::fs::ExclusionRules::from_text(job->exclusions);
        const auto summary = engine.run(job_id_, job->source_root, rules);
        repo.prune_snapshots(job_id_,
                             static_cast<std::size_t>(std::max(1, job->retention_keep)));

        const auto referenced = repo.all_referenced_digests();
        store.collect_garbage(
            std::unordered_set<std::string>(referenced.begin(), referenced.end()));

        const std::string label = job->name.empty() ? job->source_root : job->name;
        notifications_->post(
            "backup",
            summary.errors > 0 ? nexus::notify::Severity::Warning
                               : nexus::notify::Severity::Success,
            "Backup complete: " + label,
            std::to_string(summary.file_count) + " files, " + mib(summary.new_bytes) +
                " MiB new");
    }

private:
    nexus::db::Database* db_;
    nexus::core::Uuid job_id_;
    nexus::notify::NotificationCenter* notifications_;
    const std::atomic<bool>* network_reachable_;
    std::atomic<bool> active_{true};
    bool paused_notified_ = false;
};

BackupModule::BackupModule() = default;
BackupModule::~BackupModule() = default;

void BackupModule::apply_migrations(nexus::db::Database& db) {
    nexus::db::migrate(db, "backup", backup_migrations());
}

void BackupModule::start(nexus::services::ServiceContext& ctx) {
    ctx_ = &ctx;
    auto* db = &ctx.db;
    report_id_ = ctx.reports.register_generator(
        kBackupReportKind, "Backup report", std::string(id()),
        [db](ReportFormat format) {
            BackupRepository repo(*db);
            return render(repo, format);
        });
    report_registered_ = true;

    BackupRepository repo(ctx.db);
    for (const BackupJob& job : repo.list_jobs()) {
        arm(job);
    }

    duplicates_token_ = ctx.events.subscribe<nexus::services::events::DuplicatesFoundEvent>(
        [this](const nexus::services::events::DuplicatesFoundEvent& e) {
            latest_duplicate_bytes_.store(static_cast<std::int64_t>(e.reclaimable_bytes),
                                          std::memory_order_relaxed);
        });
    subscribed_to_duplicates_ = true;

    connectivity_token_ = ctx.events.subscribe<nexus::services::events::ConnectivityStateEvent>(
        [this](const nexus::services::events::ConnectivityStateEvent& e) {
            network_reachable_.store(e.internet_reachable, std::memory_order_relaxed);
        });
    subscribed_to_connectivity_ = true;
}

void BackupModule::arm(const BackupJob& job) {
    if (ctx_ == nullptr || !job.enabled) {
        return;
    }
    const auto interval = parse_schedule(job.schedule);
    if (!interval) {
        return;
    }
    auto task = std::make_shared<ScheduledBackup>(ctx_->db, job.id, ctx_->notifications,
                                                  &network_reachable_);
    scheduled_[job.id] = task;
    schedule_ids_[job.id] =
        ctx_->scheduler.schedule_every(*interval, [task] { task->tick(); }, *interval);
}

void BackupModule::disarm(const nexus::core::Uuid& job_id) {
    if (const auto sid = schedule_ids_.find(job_id); sid != schedule_ids_.end()) {
        if (ctx_ != nullptr) {
            ctx_->scheduler.cancel(sid->second);
        }
        schedule_ids_.erase(sid);
    }
    if (const auto task = scheduled_.find(job_id); task != scheduled_.end()) {
        task->second->set_active(false);
        scheduled_.erase(task);
    }
}

void BackupModule::reschedule_job(const nexus::core::Uuid& job_id) {
    if (ctx_ == nullptr) {
        return;
    }
    disarm(job_id);
    BackupRepository repo(ctx_->db);
    if (const auto job = repo.find_job(job_id)) {
        arm(*job);
    }
}

void BackupModule::stop() {
    for (const auto& [job_id, sid] : schedule_ids_) {
        if (ctx_ != nullptr) {
            ctx_->scheduler.cancel(sid);
        }
    }
    schedule_ids_.clear();
    for (auto& [job_id, task] : scheduled_) {
        task->set_active(false);
    }
    scheduled_.clear();

    if (subscribed_to_duplicates_ && ctx_ != nullptr) {
        ctx_->events.unsubscribe(duplicates_token_);
        subscribed_to_duplicates_ = false;
    }
    if (subscribed_to_connectivity_ && ctx_ != nullptr) {
        ctx_->events.unsubscribe(connectivity_token_);
        subscribed_to_connectivity_ = false;
    }

    if (report_registered_ && ctx_ != nullptr) {
        ctx_->reports.unregister(report_id_);
        report_registered_ = false;
    }
}

std::optional<std::uint64_t> BackupModule::latest_known_duplicate_bytes() const noexcept {
    const auto value = latest_duplicate_bytes_.load(std::memory_order_relaxed);
    if (value < 0) {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(value);
}

} // namespace nexus::module::backup
