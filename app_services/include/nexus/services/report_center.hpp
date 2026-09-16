#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "nexus/core/id.hpp"
#include "nexus/core/time.hpp"

namespace nexus::db {
class Database;
}

namespace nexus::services {

enum class ReportFormat {
    Html,
    Csv,
};

[[nodiscard]] std::string_view to_string(ReportFormat format) noexcept;
[[nodiscard]] std::string_view file_extension(ReportFormat format) noexcept;

/// A generated report, recorded in the `reports` table with its file on disk.
struct ReportRecord {
    nexus::core::Uuid id;
    std::string module;
    std::string kind;
    std::string title;
    std::string format; ///< "html" | "csv"
    std::filesystem::path path;
    nexus::core::Timestamp created_at{};
};

/// One report center for the suite (UFR-006). Modules register a renderer for a
/// report kind; generate() runs it, writes the file, and records the row.
class ReportCenter {
public:
    using Renderer = std::function<std::string(ReportFormat)>;

    struct GeneratorId {
        std::uint64_t value = 0;
        friend bool operator==(const GeneratorId&, const GeneratorId&) = default;
    };

    struct GeneratorInfo {
        std::string kind;
        std::string title;
        std::string module;
    };

    ReportCenter(nexus::db::Database& db, std::filesystem::path output_dir);

    /// Registers a renderer. `kind` must be unique; re-registering replaces it.
    GeneratorId register_generator(std::string kind, std::string title, std::string module,
                                   Renderer renderer);
    void unregister(GeneratorId id);

    [[nodiscard]] std::vector<GeneratorInfo> generators() const;

    /// Runs the generator for `kind`, writes `<output_dir>/<kind>-<stamp>.<ext>`,
    /// records it, and returns the row. Throws std::runtime_error if `kind` is
    /// unknown.
    ReportRecord generate(std::string_view kind, ReportFormat format);

    [[nodiscard]] std::vector<ReportRecord> recent(std::size_t limit = 50) const;

    /// Deletes reports (row + file on disk) created before `cutoff`. A
    /// missing file is not an error - the row is removed either way. Returns
    /// rows removed.
    std::size_t prune_before(nexus::core::Timestamp cutoff);

private:
    struct Entry {
        GeneratorId id;
        std::string kind;
        std::string title;
        std::string module;
        Renderer renderer;
    };

    nexus::db::Database* db_;
    std::filesystem::path output_dir_;
    mutable std::mutex mutex_;
    std::vector<Entry> entries_;
    std::uint64_t next_id_ = 1;
};

} // namespace nexus::services
