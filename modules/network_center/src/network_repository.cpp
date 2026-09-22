#include "nexus/module/network_center/network_repository.hpp"

#include <string>

#include "nexus/db/database.hpp"
#include "nexus/db/statement.hpp"
#include "nexus/db/transaction.hpp"

namespace nexus::module::network_center {

namespace {

std::optional<std::chrono::microseconds> read_rtt(nexus::db::Statement& stmt, int col) {
    if (stmt.column_is_null(col)) {
        return std::nullopt;
    }
    return std::chrono::microseconds{stmt.column_int64(col)};
}

NetworkRange read_network(nexus::db::Statement& stmt) {
    NetworkRange range;
    range.id = stmt.column_int64(0);
    range.cidr = stmt.column_text(1);
    range.label = stmt.column_text(2);
    if (const auto at = nexus::core::from_iso8601(stmt.column_text(3))) {
        range.created_at = *at;
    }
    return range;
}

Device read_device(nexus::db::Statement& stmt) {
    Device device;
    device.id = stmt.column_int64(0);
    device.network_id = stmt.column_int64(1);
    device.address = stmt.column_text(2);
    device.hostname = stmt.column_text(3);
    device.label = stmt.column_text(4);
    device.status = stmt.column_text(5);
    if (const auto at = nexus::core::from_iso8601(stmt.column_text(6))) {
        device.first_seen_at = *at;
    }
    if (const auto at = nexus::core::from_iso8601(stmt.column_text(7))) {
        device.last_seen_at = *at;
    }
    device.open_ports = stmt.column_text(8);
    return device;
}

} // namespace

std::vector<NetworkRange> NetworkRepository::networks() const {
    nexus::db::Statement stmt =
        db_->prepare("SELECT id, cidr, label, created_at FROM networks ORDER BY id");
    std::vector<NetworkRange> out;
    while (stmt.step()) {
        out.push_back(read_network(stmt));
    }
    return out;
}

std::optional<NetworkRange> NetworkRepository::find_network(std::int64_t id) const {
    nexus::db::Statement stmt =
        db_->prepare("SELECT id, cidr, label, created_at FROM networks WHERE id = ?");
    stmt.bind(1, id);
    if (!stmt.step()) {
        return std::nullopt;
    }
    return read_network(stmt);
}

std::int64_t NetworkRepository::add_network(std::string_view cidr, std::string_view label) {
    nexus::db::Statement stmt = db_->prepare(
        "INSERT INTO networks (cidr, label, created_at) VALUES (?, ?, ?)");
    stmt.bind(1, cidr);
    stmt.bind(2, label);
    stmt.bind(3, nexus::core::to_iso8601(nexus::core::now()));
    stmt.step();
    return stmt.last_insert_rowid();
}

bool NetworkRepository::delete_network(std::int64_t id) {
    nexus::db::Statement stmt = db_->prepare("DELETE FROM networks WHERE id = ?");
    stmt.bind(1, id);
    stmt.step();
    return stmt.changes() > 0;
}

std::vector<Device> NetworkRepository::devices(std::optional<std::int64_t> network_id) const {
    std::string sql =
        "SELECT id, network_id, address, hostname, label, status, first_seen_at, last_seen_at, "
        "open_ports "
        "FROM devices";
    if (network_id.has_value()) {
        sql += " WHERE network_id = ?";
    }
    sql += " ORDER BY network_id, address";

    nexus::db::Statement stmt = db_->prepare(sql);
    if (network_id.has_value()) {
        stmt.bind(1, *network_id);
    }
    std::vector<Device> out;
    while (stmt.step()) {
        out.push_back(read_device(stmt));
    }
    return out;
}

std::optional<Device> NetworkRepository::find_device(std::int64_t id) const {
    nexus::db::Statement stmt = db_->prepare(
        "SELECT id, network_id, address, hostname, label, status, first_seen_at, last_seen_at, "
        "open_ports "
        "FROM devices WHERE id = ?");
    stmt.bind(1, id);
    if (!stmt.step()) {
        return std::nullopt;
    }
    return read_device(stmt);
}

std::optional<Device> NetworkRepository::find_device_by_address(std::int64_t network_id,
                                                                 std::string_view address) const {
    nexus::db::Statement stmt = db_->prepare(
        "SELECT id, network_id, address, hostname, label, status, first_seen_at, last_seen_at, "
        "open_ports "
        "FROM devices WHERE network_id = ? AND address = ?");
    stmt.bind(1, network_id);
    stmt.bind(2, address);
    if (!stmt.step()) {
        return std::nullopt;
    }
    return read_device(stmt);
}

std::int64_t NetworkRepository::upsert_device(std::int64_t network_id, std::string_view address,
                                              std::string_view hostname,
                                              nexus::core::Timestamp at) {
    const std::string stamp = nexus::core::to_iso8601(at);
    nexus::db::Transaction tx(*db_);

    if (const auto existing = find_device_by_address(network_id, address)) {
        nexus::db::Statement update = db_->prepare(
            "UPDATE devices SET status = 'online', last_seen_at = ?, "
            "hostname = CASE WHEN ? != '' THEN ? ELSE hostname END WHERE id = ?");
        update.bind(1, stamp);
        update.bind(2, hostname);
        update.bind(3, hostname);
        update.bind(4, existing->id);
        update.step();
        tx.commit();
        return existing->id;
    }

    nexus::db::Statement insert = db_->prepare(
        "INSERT INTO devices (network_id, address, hostname, status, first_seen_at, last_seen_at) "
        "VALUES (?, ?, ?, 'online', ?, ?)");
    insert.bind(1, network_id);
    insert.bind(2, address);
    insert.bind(3, hostname);
    insert.bind(4, stamp);
    insert.bind(5, stamp);
    insert.step();
    const std::int64_t id = insert.last_insert_rowid();
    tx.commit();
    return id;
}

void NetworkRepository::set_device_status(std::int64_t device_id, std::string_view status,
                                          nexus::core::Timestamp at) {
    nexus::db::Statement stmt =
        status == "online"
            ? db_->prepare("UPDATE devices SET status = ?, last_seen_at = ? WHERE id = ?")
            : db_->prepare("UPDATE devices SET status = ? WHERE id = ?");
    stmt.bind(1, status);
    if (status == "online") {
        stmt.bind(2, nexus::core::to_iso8601(at));
        stmt.bind(3, device_id);
    } else {
        stmt.bind(2, device_id);
    }
    stmt.step();
}

void NetworkRepository::set_device_open_ports(std::int64_t device_id, std::string_view ports) {
    nexus::db::Statement stmt = db_->prepare("UPDATE devices SET open_ports = ? WHERE id = ?");
    stmt.bind(1, ports);
    stmt.bind(2, device_id);
    stmt.step();
}

void NetworkRepository::rename_device(std::int64_t device_id, std::string_view label) {
    nexus::db::Statement stmt = db_->prepare("UPDATE devices SET label = ? WHERE id = ?");
    stmt.bind(1, label);
    stmt.bind(2, device_id);
    stmt.step();
}

bool NetworkRepository::delete_device(std::int64_t device_id) {
    nexus::db::Statement stmt = db_->prepare("DELETE FROM devices WHERE id = ?");
    stmt.bind(1, device_id);
    stmt.step();
    return stmt.changes() > 0;
}

std::int64_t NetworkRepository::begin_check(std::int64_t network_id, std::string_view kind,
                                            nexus::core::Timestamp at) {
    nexus::db::Statement stmt = db_->prepare(
        "INSERT INTO checks (network_id, kind, started_at) VALUES (?, ?, ?)");
    stmt.bind(1, network_id);
    stmt.bind(2, kind);
    stmt.bind(3, nexus::core::to_iso8601(at));
    stmt.step();
    return stmt.last_insert_rowid();
}

void NetworkRepository::finish_check(std::int64_t check_id, int devices_found,
                                     nexus::core::Timestamp at) {
    nexus::db::Statement stmt = db_->prepare(
        "UPDATE checks SET finished_at = ?, devices_found = ? WHERE id = ?");
    stmt.bind(1, nexus::core::to_iso8601(at));
    stmt.bind(2, static_cast<std::int64_t>(devices_found));
    stmt.bind(3, check_id);
    stmt.step();
}

void NetworkRepository::record_check_result(std::int64_t check_id, std::int64_t device_id,
                                            std::string_view address, std::string_view status,
                                            std::optional<std::chrono::microseconds> rtt,
                                            nexus::core::Timestamp at) {
    nexus::db::Statement stmt = db_->prepare(
        "INSERT INTO check_results (check_id, device_id, address, status, rtt_us, checked_at) "
        "VALUES (?, ?, ?, ?, ?, ?)");
    stmt.bind(1, check_id);
    stmt.bind(2, device_id);
    stmt.bind(3, address);
    stmt.bind(4, status);
    if (rtt.has_value()) {
        stmt.bind(5, static_cast<std::int64_t>(rtt->count()));
    } else {
        stmt.bind(5, nullptr);
    }
    stmt.bind(6, nexus::core::to_iso8601(at));
    stmt.step();
}

std::vector<CheckResult> NetworkRepository::recent_results(std::int64_t device_id,
                                                            std::size_t limit) const {
    nexus::db::Statement stmt = db_->prepare(
        "SELECT id, check_id, device_id, address, status, rtt_us, checked_at FROM check_results "
        "WHERE device_id = ? ORDER BY checked_at DESC, id DESC LIMIT ?");
    stmt.bind(1, device_id);
    stmt.bind(2, static_cast<std::int64_t>(limit));

    std::vector<CheckResult> out;
    while (stmt.step()) {
        CheckResult result;
        result.id = stmt.column_int64(0);
        result.check_id = stmt.column_int64(1);
        result.device_id = stmt.column_int64(2);
        result.address = stmt.column_text(3);
        result.status = stmt.column_text(4);
        result.rtt = read_rtt(stmt, 5);
        if (const auto at = nexus::core::from_iso8601(stmt.column_text(6))) {
            result.checked_at = *at;
        }
        out.push_back(std::move(result));
    }
    return out;
}

std::int64_t NetworkRepository::prune_before(nexus::core::Timestamp cutoff) {
    const std::string stamp = nexus::core::to_iso8601(cutoff);
    nexus::db::Transaction tx(*db_);

    nexus::db::Statement results = db_->prepare("DELETE FROM check_results WHERE checked_at < ?");
    results.bind(1, stamp);
    results.step();
    std::int64_t removed = db_->changes();

    nexus::db::Statement checks =
        db_->prepare("DELETE FROM checks WHERE finished_at IS NOT NULL AND finished_at < ?");
    checks.bind(1, stamp);
    checks.step();
    removed += db_->changes();

    tx.commit();
    return removed;
}

} // namespace nexus::module::network_center
