#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "nexus/core/time.hpp"

namespace nexus::db {
class Database;
struct Migration;
}

namespace nexus::module::network_center {

[[nodiscard]] std::span<const nexus::db::Migration> network_center_migrations();

struct NetworkRange {
    std::int64_t id = 0;
    std::string cidr;
    std::string label;
    nexus::core::Timestamp created_at{};
};

struct Device {
    std::int64_t id = 0;
    std::int64_t network_id = 0;
    std::string address;
    std::string hostname;
    std::string label;
    std::string status; ///< "online" | "offline" | "unknown"
    nexus::core::Timestamp first_seen_at{};
    nexus::core::Timestamp last_seen_at{};

    [[nodiscard]] bool online() const noexcept { return status == "online"; }
};

struct CheckResult {
    std::int64_t id = 0;
    std::int64_t check_id = 0;
    std::int64_t device_id = 0;
    std::string address;
    std::string status;
    std::optional<std::chrono::microseconds> rtt;
    nexus::core::Timestamp checked_at{};
};

class NetworkRepository {
public:
    explicit NetworkRepository(nexus::db::Database& db) noexcept : db_(&db) {}

    // Authorized ranges. A device is only ever discovered or monitored under a
    // range the user explicitly added here (UFR-015: no silent auto-scan).
    [[nodiscard]] std::vector<NetworkRange> networks() const;
    [[nodiscard]] std::optional<NetworkRange> find_network(std::int64_t id) const;
    std::int64_t add_network(std::string_view cidr, std::string_view label);
    bool delete_network(std::int64_t id);

    [[nodiscard]] std::vector<Device> devices(std::optional<std::int64_t> network_id = std::nullopt) const;
    [[nodiscard]] std::optional<Device> find_device(std::int64_t id) const;
    [[nodiscard]] std::optional<Device> find_device_by_address(std::int64_t network_id,
                                                               std::string_view address) const;

    /// Records that `address` answered on `network_id`: inserts a new device
    /// (status "online") or refreshes an existing one's hostname/last_seen_at
    /// and marks it online. Returns the device id.
    std::int64_t upsert_device(std::int64_t network_id, std::string_view address,
                               std::string_view hostname, nexus::core::Timestamp at);
    void set_device_status(std::int64_t device_id, std::string_view status,
                           nexus::core::Timestamp at);
    void rename_device(std::int64_t device_id, std::string_view label);
    bool delete_device(std::int64_t device_id);

    std::int64_t begin_check(std::int64_t network_id, std::string_view kind,
                             nexus::core::Timestamp at);
    void finish_check(std::int64_t check_id, int devices_found, nexus::core::Timestamp at);
    void record_check_result(std::int64_t check_id, std::int64_t device_id,
                             std::string_view address, std::string_view status,
                             std::optional<std::chrono::microseconds> rtt,
                             nexus::core::Timestamp at);
    [[nodiscard]] std::vector<CheckResult> recent_results(std::int64_t device_id,
                                                          std::size_t limit = 50) const;

    std::int64_t prune_before(nexus::core::Timestamp cutoff);

private:
    nexus::db::Database* db_;
};

} // namespace nexus::module::network_center
