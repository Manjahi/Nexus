#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "nexus/core/time.hpp"

namespace nexus::db {
class Database;
struct Migration;
}

namespace nexus::module::hardware {

/// Hardware module schema migrations. Apply under the component name "hardware".
[[nodiscard]] std::span<const nexus::db::Migration> hardware_migrations();

struct MetricSample {
    std::string metric;
    std::string scope;
    double value = 0.0;
};

struct MetricPoint {
    nexus::core::Timestamp at{};
    double value = 0.0;
};

struct ProcessSample {
    std::uint32_t pid = 0;
    std::string name;
    double cpu_fraction = 0.0;
    std::uint64_t working_set_bytes = 0;
};

enum class Comparison { GreaterThan, LessThan };

struct Threshold {
    std::string id;
    std::string metric;
    std::string scope;
    Comparison comparison = Comparison::GreaterThan;
    double value = 0.0;
    std::string severity; ///< "info" | "warning" | "error"
    bool enabled = true;

    [[nodiscard]] bool breached_by(double reading) const noexcept {
        return comparison == Comparison::GreaterThan ? reading > value : reading < value;
    }
};

/// Reads/writes the Health tables (metric_samples, process_samples, thresholds).
class HardwareRepository {
public:
    explicit HardwareRepository(nexus::db::Database& db) noexcept : db_(&db) {}

    void record_metrics(std::span<const MetricSample> samples, nexus::core::Timestamp at);
    void record_processes(std::span<const ProcessSample> processes, nexus::core::Timestamp at);

    [[nodiscard]] std::vector<MetricPoint> metric_series(std::string_view metric,
                                                        std::string_view scope,
                                                        nexus::core::Timestamp since) const;
    [[nodiscard]] std::vector<ProcessSample> latest_processes(std::size_t limit = 20) const;

    [[nodiscard]] std::vector<Threshold> thresholds(bool enabled_only = false) const;
    void upsert_threshold(const Threshold& threshold);
    bool delete_threshold(std::string_view id);

    /// Deletes metric and process samples older than `cutoff`. Returns rows removed.
    std::int64_t prune_before(nexus::core::Timestamp cutoff);

private:
    nexus::db::Database* db_;
};

} // namespace nexus::module::hardware
