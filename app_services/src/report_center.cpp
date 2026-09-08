#include "nexus/services/report_center.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <utility>

#include "nexus/db/database.hpp"
#include "nexus/db/statement.hpp"

#include "support.hpp"

namespace nexus::services {

std::string_view to_string(ReportFormat format) noexcept {
    return format == ReportFormat::Csv ? "csv" : "html";
}

std::string_view file_extension(ReportFormat format) noexcept {
    return format == ReportFormat::Csv ? "csv" : "html";
}

namespace {

std::string filename_stamp(nexus::core::Timestamp at) {
    std::string s = nexus::core::to_iso8601(at); // 2026-09-08T12:34:56Z
    std::replace(s.begin(), s.end(), ':', '-');
    if (!s.empty() && s.back() == 'Z') {
        s.pop_back();
    }
    return s;
}

} // namespace

ReportCenter::ReportCenter(nexus::db::Database& db, std::filesystem::path output_dir)
    : db_(&db), output_dir_(std::move(output_dir)) {}

ReportCenter::GeneratorId ReportCenter::register_generator(std::string kind, std::string title,
                                                          std::string module, Renderer renderer) {
    const std::scoped_lock lock(mutex_);
    std::erase_if(entries_, [&](const Entry& e) { return e.kind == kind; });
    const GeneratorId id{next_id_++};
    entries_.push_back(Entry{id, std::move(kind), std::move(title), std::move(module),
                             std::move(renderer)});
    return id;
}

void ReportCenter::unregister(GeneratorId id) {
    const std::scoped_lock lock(mutex_);
    std::erase_if(entries_, [&](const Entry& e) { return e.id == id; });
}

std::vector<ReportCenter::GeneratorInfo> ReportCenter::generators() const {
    const std::scoped_lock lock(mutex_);
    std::vector<GeneratorInfo> out;
    out.reserve(entries_.size());
    for (const Entry& e : entries_) {
        out.push_back({e.kind, e.title, e.module});
    }
    return out;
}

ReportRecord ReportCenter::generate(std::string_view kind, ReportFormat format) {
    Entry entry;
    {
        const std::scoped_lock lock(mutex_);
        const auto it = std::find_if(entries_.begin(), entries_.end(),
                                     [&](const Entry& e) { return e.kind == kind; });
        if (it == entries_.end()) {
            throw std::runtime_error("no report generator for kind: " + std::string(kind));
        }
        entry = *it;
    }

    const std::string content = entry.renderer(format);

    const nexus::core::Timestamp now = nexus::core::now();
    std::filesystem::create_directories(output_dir_);
    const std::filesystem::path path =
        output_dir_ / (entry.kind + "-" + filename_stamp(now) + "." +
                       std::string(file_extension(format)));

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error("cannot write report file: " + path.string());
        }
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
    }

    ReportRecord record;
    record.id = nexus::core::Uuid::generate();
    record.module = entry.module;
    record.kind = entry.kind;
    record.title = entry.title;
    record.format = std::string(to_string(format));
    record.path = path;
    record.created_at = now;

    nexus::db::Statement stmt = db_->prepare(
        "INSERT INTO reports (id, module, kind, title, format, path, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)");
    stmt.bind(1, record.id.to_string());
    detail::bind_text_or_null(stmt, 2, record.module);
    stmt.bind(3, record.kind);
    stmt.bind(4, record.title);
    stmt.bind(5, record.format);
    stmt.bind(6, record.path.string());
    stmt.bind(7, nexus::core::to_iso8601(now));
    stmt.step();

    return record;
}

std::vector<ReportRecord> ReportCenter::recent(std::size_t limit) const {
    nexus::db::Statement stmt = db_->prepare(
        "SELECT id, module, kind, title, format, path, created_at FROM reports "
        "ORDER BY created_at DESC, rowid DESC LIMIT ?");
    stmt.bind(1, static_cast<std::int64_t>(limit));

    std::vector<ReportRecord> out;
    while (stmt.step()) {
        ReportRecord record;
        if (const auto parsed = nexus::core::Uuid::parse(stmt.column_text(0))) {
            record.id = *parsed;
        }
        record.module = stmt.column_is_null(1) ? std::string{} : stmt.column_text(1);
        record.kind = stmt.column_text(2);
        record.title = stmt.column_text(3);
        record.format = stmt.column_text(4);
        record.path = stmt.column_is_null(5) ? std::string{} : stmt.column_text(5);
        if (const auto at = nexus::core::from_iso8601(stmt.column_text(6))) {
            record.created_at = *at;
        }
        out.push_back(std::move(record));
    }
    return out;
}

} // namespace nexus::services
