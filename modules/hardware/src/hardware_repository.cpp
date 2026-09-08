#include "nexus/module/hardware/hardware_repository.hpp"

#include <string>

#include "nexus/db/database.hpp"
#include "nexus/db/statement.hpp"
#include "nexus/db/transaction.hpp"

namespace nexus::module::hardware {

namespace {

const char* comparison_to_sql(Comparison c) {
    return c == Comparison::GreaterThan ? "gt" : "lt";
}

Comparison comparison_from_sql(std::string_view text) {
    return text == "lt" ? Comparison::LessThan : Comparison::GreaterThan;
}

} // namespace

void HardwareRepository::record_metrics(std::span<const MetricSample> samples,
                                        nexus::core::Timestamp at) {
    if (samples.empty()) {
        return;
    }
    const std::string stamp = nexus::core::to_iso8601(at);
    nexus::db::Transaction tx(*db_);
    nexus::db::Statement stmt = db_->prepare(
        "INSERT INTO metric_samples (sampled_at, metric, scope, value) VALUES (?, ?, ?, ?)");
    for (const MetricSample& sample : samples) {
        stmt.bind(1, stamp);
        stmt.bind(2, sample.metric);
        stmt.bind(3, sample.scope);
        stmt.bind(4, sample.value);
        stmt.step();
        stmt.reset();
    }
    tx.commit();
}

void HardwareRepository::record_processes(std::span<const ProcessSample> processes,
                                         nexus::core::Timestamp at) {
    if (processes.empty()) {
        return;
    }
    const std::string stamp = nexus::core::to_iso8601(at);
    nexus::db::Transaction tx(*db_);
    nexus::db::Statement stmt = db_->prepare(
        "INSERT INTO process_samples (sampled_at, pid, name, cpu_fraction, working_set_bytes) "
        "VALUES (?, ?, ?, ?, ?)");
    for (const ProcessSample& proc : processes) {
        stmt.bind(1, stamp);
        stmt.bind(2, static_cast<std::int64_t>(proc.pid));
        stmt.bind(3, proc.name);
        stmt.bind(4, proc.cpu_fraction);
        stmt.bind(5, static_cast<std::int64_t>(proc.working_set_bytes));
        stmt.step();
        stmt.reset();
    }
    tx.commit();
}

std::vector<MetricPoint> HardwareRepository::metric_series(std::string_view metric,
                                                          std::string_view scope,
                                                          nexus::core::Timestamp since) const {
    nexus::db::Statement stmt = db_->prepare(
        "SELECT sampled_at, value FROM metric_samples "
        "WHERE metric = ? AND scope = ? AND sampled_at >= ? ORDER BY sampled_at, id");
    stmt.bind(1, metric);
    stmt.bind(2, scope);
    stmt.bind(3, nexus::core::to_iso8601(since));

    std::vector<MetricPoint> points;
    while (stmt.step()) {
        MetricPoint point;
        if (const auto at = nexus::core::from_iso8601(stmt.column_text(0))) {
            point.at = *at;
        }
        point.value = stmt.column_double(1);
        points.push_back(point);
    }
    return points;
}

std::vector<MetricSample> HardwareRepository::latest_snapshot() const {
    nexus::db::Statement stmt = db_->prepare(
        "SELECT metric, scope, value FROM metric_samples "
        "WHERE sampled_at = (SELECT MAX(sampled_at) FROM metric_samples) "
        "ORDER BY metric, scope");
    std::vector<MetricSample> out;
    while (stmt.step()) {
        out.push_back({stmt.column_text(0), stmt.column_text(1), stmt.column_double(2)});
    }
    return out;
}

std::vector<ProcessSample> HardwareRepository::latest_processes(std::size_t limit) const {
    nexus::db::Statement latest =
        db_->prepare("SELECT sampled_at FROM process_samples ORDER BY sampled_at DESC LIMIT 1");
    if (!latest.step()) {
        return {};
    }
    const std::string stamp = latest.column_text(0);

    nexus::db::Statement stmt = db_->prepare(
        "SELECT pid, name, cpu_fraction, working_set_bytes FROM process_samples "
        "WHERE sampled_at = ? ORDER BY cpu_fraction DESC, working_set_bytes DESC LIMIT ?");
    stmt.bind(1, stamp);
    stmt.bind(2, static_cast<std::int64_t>(limit));

    std::vector<ProcessSample> processes;
    while (stmt.step()) {
        ProcessSample proc;
        proc.pid = static_cast<std::uint32_t>(stmt.column_int64(0));
        proc.name = stmt.column_text(1);
        proc.cpu_fraction = stmt.column_double(2);
        proc.working_set_bytes = static_cast<std::uint64_t>(stmt.column_int64(3));
        processes.push_back(std::move(proc));
    }
    return processes;
}

std::vector<Threshold> HardwareRepository::thresholds(bool enabled_only) const {
    std::string sql = "SELECT id, metric, scope, comparison, value, severity, enabled FROM thresholds";
    if (enabled_only) {
        sql += " WHERE enabled = 1";
    }
    sql += " ORDER BY id";

    nexus::db::Statement stmt = db_->prepare(sql);
    std::vector<Threshold> out;
    while (stmt.step()) {
        Threshold t;
        t.id = stmt.column_text(0);
        t.metric = stmt.column_text(1);
        t.scope = stmt.column_text(2);
        t.comparison = comparison_from_sql(stmt.column_text(3));
        t.value = stmt.column_double(4);
        t.severity = stmt.column_text(5);
        t.enabled = stmt.column_int64(6) != 0;
        out.push_back(std::move(t));
    }
    return out;
}

void HardwareRepository::upsert_threshold(const Threshold& threshold) {
    nexus::db::Statement stmt = db_->prepare(
        "INSERT INTO thresholds (id, metric, scope, comparison, value, severity, enabled) "
        "VALUES (?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(id) DO UPDATE SET metric = excluded.metric, scope = excluded.scope, "
        "comparison = excluded.comparison, value = excluded.value, severity = excluded.severity, "
        "enabled = excluded.enabled");
    stmt.bind(1, threshold.id);
    stmt.bind(2, threshold.metric);
    stmt.bind(3, threshold.scope);
    stmt.bind(4, comparison_to_sql(threshold.comparison));
    stmt.bind(5, threshold.value);
    stmt.bind(6, threshold.severity);
    stmt.bind(7, threshold.enabled ? 1 : 0);
    stmt.step();
}

bool HardwareRepository::delete_threshold(std::string_view id) {
    nexus::db::Statement stmt = db_->prepare("DELETE FROM thresholds WHERE id = ?");
    stmt.bind(1, id);
    stmt.step();
    return stmt.changes() > 0;
}

std::int64_t HardwareRepository::prune_before(nexus::core::Timestamp cutoff) {
    const std::string stamp = nexus::core::to_iso8601(cutoff);
    nexus::db::Transaction tx(*db_);

    std::int64_t removed = 0;
    nexus::db::Statement m = db_->prepare("DELETE FROM metric_samples WHERE sampled_at < ?");
    m.bind(1, stamp);
    m.step();
    removed += db_->changes();

    nexus::db::Statement p = db_->prepare("DELETE FROM process_samples WHERE sampled_at < ?");
    p.bind(1, stamp);
    p.step();
    removed += db_->changes();

    tx.commit();
    return removed;
}

} // namespace nexus::module::hardware
