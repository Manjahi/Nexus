#include "nexus/module/connectivity/connectivity_repository.hpp"

#include <string>

#include "nexus/db/database.hpp"
#include "nexus/db/statement.hpp"
#include "nexus/db/transaction.hpp"

namespace nexus::module::connectivity {

std::string_view to_string(ProbeKind kind) noexcept {
    switch (kind) {
        case ProbeKind::Icmp:
            return "icmp";
        case ProbeKind::Tcp:
            return "tcp";
        case ProbeKind::Http:
            return "http";
        case ProbeKind::Dns:
            return "dns";
    }
    return "icmp";
}

std::optional<ProbeKind> probe_kind_from_string(std::string_view text) noexcept {
    if (text == "icmp") {
        return ProbeKind::Icmp;
    }
    if (text == "tcp") {
        return ProbeKind::Tcp;
    }
    if (text == "http") {
        return ProbeKind::Http;
    }
    if (text == "dns") {
        return ProbeKind::Dns;
    }
    return std::nullopt;
}

namespace {

std::optional<std::chrono::microseconds> read_rtt(nexus::db::Statement& stmt, int col) {
    if (stmt.column_is_null(col)) {
        return std::nullopt;
    }
    return std::chrono::microseconds{stmt.column_int64(col)};
}

} // namespace

std::vector<ProbeTarget> ConnectivityRepository::targets(bool enabled_only) const {
    std::string sql = "SELECT id, kind, address, port, label, enabled FROM probe_targets";
    if (enabled_only) {
        sql += " WHERE enabled = 1";
    }
    sql += " ORDER BY id";

    nexus::db::Statement stmt = db_->prepare(sql);
    std::vector<ProbeTarget> out;
    while (stmt.step()) {
        ProbeTarget target;
        target.id = stmt.column_text(0);
        target.kind = probe_kind_from_string(stmt.column_text(1)).value_or(ProbeKind::Icmp);
        target.address = stmt.column_text(2);
        if (!stmt.column_is_null(3)) {
            target.port = static_cast<std::uint16_t>(stmt.column_int64(3));
        }
        target.label = stmt.column_text(4);
        target.enabled = stmt.column_int64(5) != 0;
        out.push_back(std::move(target));
    }
    return out;
}

void ConnectivityRepository::upsert_target(const ProbeTarget& target) {
    nexus::db::Statement stmt = db_->prepare(
        "INSERT INTO probe_targets (id, kind, address, port, label, enabled) "
        "VALUES (?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(id) DO UPDATE SET kind = excluded.kind, address = excluded.address, "
        "port = excluded.port, label = excluded.label, enabled = excluded.enabled");
    stmt.bind(1, target.id);
    stmt.bind(2, to_string(target.kind));
    stmt.bind(3, target.address);
    if (target.port.has_value()) {
        stmt.bind(4, static_cast<std::int64_t>(*target.port));
    } else {
        stmt.bind(4, nullptr);
    }
    stmt.bind(5, target.label);
    stmt.bind(6, target.enabled ? 1 : 0);
    stmt.step();
}

bool ConnectivityRepository::delete_target(std::string_view id) {
    nexus::db::Statement stmt = db_->prepare("DELETE FROM probe_targets WHERE id = ?");
    stmt.bind(1, id);
    stmt.step();
    return stmt.changes() > 0;
}

void ConnectivityRepository::record_samples(std::span<const ConnectivitySample> samples,
                                            nexus::core::Timestamp at) {
    if (samples.empty()) {
        return;
    }
    const std::string stamp = nexus::core::to_iso8601(at);
    nexus::db::Transaction tx(*db_);
    nexus::db::Statement stmt = db_->prepare(
        "INSERT INTO connectivity_samples (sampled_at, target_id, status, rtt_us, detail) "
        "VALUES (?, ?, ?, ?, ?)");
    for (const ConnectivitySample& sample : samples) {
        stmt.bind(1, stamp);
        stmt.bind(2, sample.target_id);
        stmt.bind(3, sample.status);
        if (sample.rtt.has_value()) {
            stmt.bind(4, static_cast<std::int64_t>(sample.rtt->count()));
        } else {
            stmt.bind(4, nullptr);
        }
        stmt.bind(5, sample.detail);
        stmt.step();
        stmt.reset();
    }
    tx.commit();
}

std::vector<SamplePoint> ConnectivityRepository::samples_since(std::string_view target_id,
                                                              nexus::core::Timestamp since) const {
    nexus::db::Statement stmt = db_->prepare(
        "SELECT sampled_at, status, rtt_us FROM connectivity_samples "
        "WHERE target_id = ? AND sampled_at >= ? ORDER BY sampled_at, id");
    stmt.bind(1, target_id);
    stmt.bind(2, nexus::core::to_iso8601(since));

    std::vector<SamplePoint> out;
    while (stmt.step()) {
        SamplePoint point;
        if (const auto at = nexus::core::from_iso8601(stmt.column_text(0))) {
            point.at = *at;
        }
        point.status = stmt.column_text(1);
        point.rtt = read_rtt(stmt, 2);
        out.push_back(std::move(point));
    }
    return out;
}

std::optional<double> ConnectivityRepository::uptime_fraction(std::string_view target_id,
                                                              nexus::core::Timestamp since) const {
    nexus::db::Statement stmt = db_->prepare(
        "SELECT COUNT(*), SUM(CASE WHEN status = 'ok' THEN 1 ELSE 0 END) "
        "FROM connectivity_samples WHERE target_id = ? AND sampled_at >= ?");
    stmt.bind(1, target_id);
    stmt.bind(2, nexus::core::to_iso8601(since));
    stmt.step();

    const std::int64_t total = stmt.column_int64(0);
    if (total == 0) {
        return std::nullopt;
    }
    const std::int64_t ok = stmt.column_int64(1);
    return static_cast<double>(ok) / static_cast<double>(total);
}

nexus::net::LatencyStats ConnectivityRepository::reliability_stats(std::string_view target_id,
                                                                    nexus::core::Timestamp since) const {
    nexus::db::Statement stmt = db_->prepare(
        "SELECT status, rtt_us FROM connectivity_samples "
        "WHERE target_id = ? AND sampled_at >= ? ORDER BY sampled_at, id");
    stmt.bind(1, target_id);
    stmt.bind(2, nexus::core::to_iso8601(since));

    std::vector<nexus::net::PingResult> samples;
    while (stmt.step()) {
        nexus::net::PingResult sample;
        // DB status strings are nexus::net::to_string(ProbeStatus) output
        // (see default_probe.cpp's status_of()) - summarize() only cares
        // about ok-vs-not, so any non-Ok status works here.
        const bool ok = stmt.column_text(0) == "ok";
        sample.status = ok ? nexus::net::ProbeStatus::Ok : nexus::net::ProbeStatus::Error;
        if (ok && !stmt.column_is_null(1)) {
            sample.rtt = std::chrono::microseconds{stmt.column_int64(1)};
        }
        samples.push_back(sample);
    }
    return nexus::net::summarize(samples);
}

std::optional<std::int64_t> ConnectivityRepository::open_outage(std::string_view target_id) const {
    nexus::db::Statement stmt = db_->prepare(
        "SELECT id FROM outages WHERE target_id = ? AND ended_at IS NULL "
        "ORDER BY started_at DESC LIMIT 1");
    stmt.bind(1, target_id);
    if (!stmt.step()) {
        return std::nullopt;
    }
    return stmt.column_int64(0);
}

std::int64_t ConnectivityRepository::begin_outage(std::string_view target_id,
                                                 nexus::core::Timestamp at) {
    nexus::db::Statement stmt = db_->prepare(
        "INSERT INTO outages (target_id, started_at, samples_failed) VALUES (?, ?, 1)");
    stmt.bind(1, target_id);
    stmt.bind(2, nexus::core::to_iso8601(at));
    stmt.step();
    return stmt.last_insert_rowid();
}

void ConnectivityRepository::bump_outage(std::int64_t outage_id) {
    nexus::db::Statement stmt =
        db_->prepare("UPDATE outages SET samples_failed = samples_failed + 1 WHERE id = ?");
    stmt.bind(1, outage_id);
    stmt.step();
}

void ConnectivityRepository::end_outage(std::int64_t outage_id, nexus::core::Timestamp at) {
    nexus::db::Statement stmt =
        db_->prepare("UPDATE outages SET ended_at = ? WHERE id = ? AND ended_at IS NULL");
    stmt.bind(1, nexus::core::to_iso8601(at));
    stmt.bind(2, outage_id);
    stmt.step();
}

std::vector<Outage> ConnectivityRepository::recent_outages(std::size_t limit) const {
    nexus::db::Statement stmt = db_->prepare(
        "SELECT id, target_id, started_at, ended_at, samples_failed FROM outages "
        "ORDER BY started_at DESC, id DESC LIMIT ?");
    stmt.bind(1, static_cast<std::int64_t>(limit));

    std::vector<Outage> out;
    while (stmt.step()) {
        Outage outage;
        outage.id = stmt.column_int64(0);
        outage.target_id = stmt.column_text(1);
        if (const auto at = nexus::core::from_iso8601(stmt.column_text(2))) {
            outage.started_at = *at;
        }
        if (!stmt.column_is_null(3)) {
            outage.ended_at = nexus::core::from_iso8601(stmt.column_text(3));
        }
        outage.samples_failed = static_cast<int>(stmt.column_int64(4));
        out.push_back(std::move(outage));
    }
    return out;
}

std::int64_t ConnectivityRepository::prune_before(nexus::core::Timestamp cutoff) {
    const std::string stamp = nexus::core::to_iso8601(cutoff);
    nexus::db::Transaction tx(*db_);

    nexus::db::Statement samples =
        db_->prepare("DELETE FROM connectivity_samples WHERE sampled_at < ?");
    samples.bind(1, stamp);
    samples.step();
    std::int64_t removed = db_->changes();

    nexus::db::Statement outages =
        db_->prepare("DELETE FROM outages WHERE ended_at IS NOT NULL AND ended_at < ?");
    outages.bind(1, stamp);
    outages.step();
    removed += db_->changes();

    tx.commit();
    return removed;
}

} // namespace nexus::module::connectivity
