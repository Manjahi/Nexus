#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "nexus/core/time.hpp"
#include "nexus/net/latency_stats.hpp"

namespace nexus::db {
class Database;
struct Migration;
}

namespace nexus::module::connectivity {

[[nodiscard]] std::span<const nexus::db::Migration> connectivity_migrations();

enum class ProbeKind { Icmp, Tcp, Http, Dns };

[[nodiscard]] std::string_view to_string(ProbeKind kind) noexcept;
[[nodiscard]] std::optional<ProbeKind> probe_kind_from_string(std::string_view text) noexcept;

struct ProbeTarget {
    std::string id;
    ProbeKind kind = ProbeKind::Icmp;
    std::string address;
    std::optional<std::uint16_t> port;
    std::string label;
    bool enabled = true;
};

struct ConnectivitySample {
    std::string target_id;
    std::string status; ///< "ok" | "timeout" | "unreachable" | "dns_failure" | "error"
    std::optional<std::chrono::microseconds> rtt;
    std::string detail;

    [[nodiscard]] bool ok() const noexcept { return status == "ok"; }
};

struct SamplePoint {
    nexus::core::Timestamp at{};
    std::string status;
    std::optional<std::chrono::microseconds> rtt;
};

struct Outage {
    std::int64_t id = 0;
    std::string target_id;
    nexus::core::Timestamp started_at{};
    std::optional<nexus::core::Timestamp> ended_at;
    int samples_failed = 0;
};

struct SpeedTestRecord {
    nexus::core::Timestamp ran_at{};
    std::optional<double> download_bps;
    std::optional<double> upload_bps; ///< not measured yet - upload speed test is a future addition
    std::optional<std::chrono::microseconds> latency;
    std::string server;
};

class ConnectivityRepository {
public:
    explicit ConnectivityRepository(nexus::db::Database& db) noexcept : db_(&db) {}

    [[nodiscard]] std::vector<ProbeTarget> targets(bool enabled_only = false) const;
    void upsert_target(const ProbeTarget& target);
    bool delete_target(std::string_view id);

    void record_samples(std::span<const ConnectivitySample> samples, nexus::core::Timestamp at);
    [[nodiscard]] std::vector<SamplePoint> samples_since(std::string_view target_id,
                                                        nexus::core::Timestamp since) const;
    /// Fraction of samples with status 'ok' for a target since `since`, or nullopt
    /// if there are no samples.
    [[nodiscard]] std::optional<double> uptime_fraction(std::string_view target_id,
                                                        nexus::core::Timestamp since) const;

    /// Packet loss and jitter over the window, computed from the same
    /// connectivity_samples rows uptime_fraction() already reads (no new
    /// capture logic) via nexus::net::summarize() - previously implemented
    /// and unit-tested but never called from anywhere.
    [[nodiscard]] nexus::net::LatencyStats reliability_stats(std::string_view target_id,
                                                             nexus::core::Timestamp since) const;

    [[nodiscard]] std::optional<std::int64_t> open_outage(std::string_view target_id) const;
    std::int64_t begin_outage(std::string_view target_id, nexus::core::Timestamp at);
    void bump_outage(std::int64_t outage_id);
    void end_outage(std::int64_t outage_id, nexus::core::Timestamp at);
    [[nodiscard]] std::vector<Outage> recent_outages(std::size_t limit = 50) const;

    /// speed_tests has existed in the schema since migration 1 but nothing
    /// ever read or wrote it - see modules/connectivity/src/speed_test.cpp.
    void record_speed_test(const SpeedTestRecord& record);
    [[nodiscard]] std::vector<SpeedTestRecord> recent_speed_tests(std::size_t limit = 50) const;

    std::int64_t prune_before(nexus::core::Timestamp cutoff);

private:
    nexus::db::Database* db_;
};

} // namespace nexus::module::connectivity
