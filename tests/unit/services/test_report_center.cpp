#include "nexus/services/report_center.hpp"

#include "nexus/db/database.hpp"
#include "nexus/db/migration.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

using nexus::services::ReportCenter;
using nexus::services::ReportFormat;

namespace {

std::filesystem::path scratch_dir() {
    const auto tag = std::chrono::steady_clock::now().time_since_epoch().count();
    auto dir = std::filesystem::temp_directory_path() /
               ("nexuspc_reports_test_" + std::to_string(tag));
    std::filesystem::remove_all(dir);
    return dir;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

nexus::db::Database migrated_db() {
    auto db = nexus::db::Database::open_in_memory();
    nexus::db::migrate(db, "core", nexus::db::core_migrations());
    return db;
}

} // namespace

TEST_CASE("generate runs the renderer, writes a file, and records a row", "[services][reports]") {
    const auto dir = scratch_dir();
    auto db = migrated_db();
    ReportCenter center(db, dir);

    center.register_generator("system-diagnostic", "System diagnostic", "hardware",
                              [](ReportFormat fmt) {
                                  return fmt == ReportFormat::Csv ? "a,b\n1,2\n"
                                                                  : "<h1>Diagnostic</h1>";
                              });

    const auto record = center.generate("system-diagnostic", ReportFormat::Html);
    REQUIRE(record.kind == "system-diagnostic");
    REQUIRE(record.format == "html");
    REQUIRE(record.module == "hardware");
    REQUIRE(std::filesystem::exists(record.path));
    REQUIRE(record.path.extension() == ".html");
    REQUIRE(read_file(record.path) == "<h1>Diagnostic</h1>");

    const auto csv = center.generate("system-diagnostic", ReportFormat::Csv);
    REQUIRE(csv.path.extension() == ".csv");
    REQUIRE(read_file(csv.path) == "a,b\n1,2\n");

    const auto recent = center.recent();
    REQUIRE(recent.size() == 2);
    REQUIRE(recent[0].kind == "system-diagnostic");

    std::filesystem::remove_all(dir);
}

TEST_CASE("generators list, replace-on-reregister, and unregister", "[services][reports]") {
    const auto dir = scratch_dir();
    auto db = migrated_db();
    ReportCenter center(db, dir);

    const auto a = center.register_generator("k", "First", "m", [](ReportFormat) { return "1"; });
    center.register_generator("k", "Second", "m", [](ReportFormat) { return "2"; });
    REQUIRE(center.generators().size() == 1);
    REQUIRE(center.generators().front().title == "Second");

    const auto b = center.register_generator("other", "Other", "m2",
                                             [](ReportFormat) { return "x"; });
    REQUIRE(center.generators().size() == 2);

    center.unregister(b);
    REQUIRE(center.generators().size() == 1);
    center.unregister(a); // stale id from the replaced generator - no-op
    REQUIRE(center.generators().size() == 1);

    std::filesystem::remove_all(dir);
}

TEST_CASE("generate throws for an unknown kind", "[services][reports]") {
    const auto dir = scratch_dir();
    auto db = migrated_db();
    ReportCenter center(db, dir);
    REQUIRE_THROWS_AS(center.generate("nope", ReportFormat::Html), std::runtime_error);
    std::filesystem::remove_all(dir);
}
