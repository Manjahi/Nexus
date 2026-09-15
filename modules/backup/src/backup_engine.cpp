#include "nexus/module/backup/backup_engine.hpp"

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

#include "nexus/fs/exclusion_rules.hpp"
#include "nexus/fs/walker.hpp"
#include "nexus/hash/hash.hpp"
#include "nexus/module/backup/backup_repository.hpp"
#include "nexus/module/backup/object_store.hpp"

namespace nexus::module::backup {

namespace fs = std::filesystem;

namespace {

std::string iso_mtime(fs::file_time_type when) {
    // Convert filesystem clock -> system clock (portable enough for a label).
    const auto sys = std::chrono::clock_cast<std::chrono::system_clock>(when);
    return nexus::core::to_iso8601(sys);
}

void emit(const Progress& progress, double fraction, std::string_view phase) {
    if (progress) {
        progress(std::clamp(fraction, 0.0, 1.0), phase);
    }
}

} // namespace

SnapshotSummary BackupEngine::run(const nexus::core::Uuid& job_id, const fs::path& source,
                                  const nexus::fs::ExclusionRules& rules, const Progress& progress,
                                  const std::function<bool()>& cancelled,
                                  const std::function<void()>& throttle) {
    SnapshotSummary summary;

    const bool persist = repo_ != nullptr;
    if (persist) {
        summary.snapshot_id = repo_->begin_snapshot(job_id);
    }

    emit(progress, 0.0, "walking");
    struct Item {
        fs::path path;
        std::uint64_t size = 0;
        fs::file_time_type mtime{};
    };
    std::vector<Item> items;
    const nexus::fs::WalkStats stats = nexus::fs::walk(
        source, rules, {},
        [&](const nexus::fs::FileEntry& e) {
            items.push_back({e.path, e.size, e.last_write_time});
        },
        cancelled);
    summary.cancelled = stats.cancelled;

    std::vector<SnapshotFile> records;
    records.reserve(items.size());

    for (std::size_t i = 0; i < items.size(); ++i) {
        if (cancelled && cancelled()) {
            summary.cancelled = true;
            break;
        }
        const Item& item = items[i];
        const auto put = store_->put_file(item.path);
        if (throttle) {
            throttle();
        }
        if (!put) {
            ++summary.errors;
            continue;
        }

        ++summary.file_count;
        summary.total_bytes += put->size;
        if (put->was_new) {
            summary.new_bytes += put->size;
        }

        SnapshotFile record;
        std::error_code ec;
        record.path = fs::relative(item.path, source, ec).generic_string();
        if (ec) {
            record.path = item.path.filename().generic_string();
        }
        record.size = put->size;
        record.mtime = iso_mtime(item.mtime);
        record.digest = nexus::hash::to_hex(put->digest);
        records.push_back(std::move(record));

        if (!items.empty()) {
            emit(progress, static_cast<double>(i + 1) / static_cast<double>(items.size()),
                 "storing");
        }
    }

    if (persist) {
        repo_->add_snapshot_files(summary.snapshot_id, records);
        repo_->finish_snapshot(summary.snapshot_id,
                               summary.cancelled ? "cancelled" : "completed", summary.file_count,
                               summary.total_bytes, summary.new_bytes);
    }
    emit(progress, 1.0, "done");
    return summary;
}

VerifyResult BackupEngine::verify(const nexus::core::Uuid& snapshot_id,
                                  const std::function<bool()>& cancelled) const {
    VerifyResult result;
    if (repo_ == nullptr) {
        return result;
    }
    for (const SnapshotFile& file : repo_->files_in(snapshot_id)) {
        if (cancelled && cancelled()) {
            break;
        }
        ++result.checked;
        const auto digest = nexus::hash::digest_from_hex(file.digest);
        if (!digest) {
            ++result.corrupt;
            continue;
        }
        if (!store_->contains(*digest)) {
            ++result.missing;
        } else if (store_->verify(*digest)) {
            ++result.ok;
        } else {
            ++result.corrupt;
        }
    }
    return result;
}

} // namespace nexus::module::backup
