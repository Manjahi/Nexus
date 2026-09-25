#include "nexus/module/backup/sync_engine.hpp"

#include "nexus/fs/exclusion_rules.hpp"
#include "nexus/fs/walker.hpp"

#include <algorithm>
#include <chrono>
#include <system_error>
#include <unordered_set>
#include <vector>

namespace nexus::module::backup {

namespace fs = std::filesystem;

namespace {

void emit(const Progress& progress, double fraction, std::string_view phase) {
    if (progress) {
        progress(std::clamp(fraction, 0.0, 1.0), phase);
    }
}

// mtime resolution differs across filesystems (FAT32 is 2s); treat anything
// within this window as "unchanged" rather than re-copying every run.
constexpr std::chrono::seconds kMtimeTolerance{2};

bool needs_copy(const fs::path& dest, std::uint64_t source_size, fs::file_time_type source_mtime) {
    std::error_code ec;
    if (!fs::exists(dest, ec) || ec) {
        return true;
    }
    const auto dest_size = fs::file_size(dest, ec);
    if (ec || dest_size != source_size) {
        return true;
    }
    const auto dest_mtime = fs::last_write_time(dest, ec);
    if (ec) {
        return true;
    }
    const auto diff =
        source_mtime > dest_mtime ? source_mtime - dest_mtime : dest_mtime - source_mtime;
    return diff > kMtimeTolerance;
}

} // namespace

SyncSummary SyncEngine::run(const fs::path& source, const fs::path& destination,
                            const nexus::fs::ExclusionRules& rules, const Progress& progress,
                            const std::function<bool()>& cancelled,
                            const std::function<void()>& throttle) {
    SyncSummary summary;

    emit(progress, 0.0, "walking");
    struct Item {
        fs::path path;
        std::uint64_t size = 0;
        fs::file_time_type mtime{};
    };
    std::vector<Item> items;
    const nexus::fs::WalkStats stats = nexus::fs::walk(
        source, rules, {},
        [&](const nexus::fs::FileEntry& e) { items.push_back({e.path, e.size, e.last_write_time}); },
        cancelled);
    summary.cancelled = stats.cancelled;

    // Tracks what must survive the deletion pass below - files by their
    // exact relative path, directories by every ancestor of a kept file (so
    // a directory that's merely empty of newly-copied files, but still holds
    // some, isn't wrongly removed).
    std::unordered_set<std::string> kept_files;
    std::unordered_set<std::string> kept_dirs;

    for (std::size_t i = 0; i < items.size(); ++i) {
        if (cancelled && cancelled()) {
            summary.cancelled = true;
            break;
        }
        const Item& item = items[i];
        std::error_code rel_ec;
        const fs::path relative = fs::relative(item.path, source, rel_ec);
        if (rel_ec) {
            ++summary.errors;
            continue;
        }
        kept_files.insert(relative.generic_string());
        for (fs::path dir = relative.parent_path(); !dir.empty(); dir = dir.parent_path()) {
            kept_dirs.insert(dir.generic_string());
        }

        const fs::path dest_path = destination / relative;
        if (needs_copy(dest_path, item.size, item.mtime)) {
            std::error_code mk_ec;
            fs::create_directories(dest_path.parent_path(), mk_ec);
            std::error_code copy_ec;
            fs::copy_file(item.path, dest_path, fs::copy_options::overwrite_existing, copy_ec);
            if (throttle) {
                throttle();
            }
            if (copy_ec) {
                ++summary.errors;
            } else {
                ++summary.files_copied;
                summary.bytes_copied += item.size;
                std::error_code mtime_ec;
                fs::last_write_time(dest_path, item.mtime, mtime_ec);
            }
        }
        if (!items.empty()) {
            emit(progress, 0.5 * static_cast<double>(i + 1) / static_cast<double>(items.size()),
                 "copying");
        }
    }

    // The one-way-sync property: anything in destination not in source gets
    // removed, not just left behind. Top-down so a stray directory can be
    // deleted wholesale (fs::remove_all) without first recursing into it.
    emit(progress, 0.5, "pruning");
    std::error_code dest_ec;
    if (fs::exists(destination, dest_ec) && !dest_ec) {
        fs::recursive_directory_iterator it(
            destination, fs::directory_options::skip_permission_denied, dest_ec);
        const fs::recursive_directory_iterator end;
        while (!dest_ec && it != end) {
            if (cancelled && cancelled()) {
                summary.cancelled = true;
                break;
            }
            std::error_code rel_ec;
            const auto relative = fs::relative(it->path(), destination, rel_ec);
            if (rel_ec) {
                it.increment(dest_ec);
                continue;
            }
            const std::string rel_str = relative.generic_string();

            std::error_code is_dir_ec;
            const bool is_dir = it->is_directory(is_dir_ec);
            if (is_dir && !is_dir_ec) {
                if (!kept_dirs.count(rel_str)) {
                    std::error_code rm_ec;
                    fs::remove_all(it->path(), rm_ec);
                    if (!rm_ec) {
                        ++summary.dirs_deleted;
                    } else {
                        ++summary.errors;
                    }
                    it.disable_recursion_pending();
                }
            } else if (!is_dir_ec) {
                if (!kept_files.count(rel_str)) {
                    std::error_code rm_ec;
                    fs::remove(it->path(), rm_ec);
                    if (!rm_ec) {
                        ++summary.files_deleted;
                    } else {
                        ++summary.errors;
                    }
                }
            }
            it.increment(dest_ec);
        }
    }

    emit(progress, 1.0, "done");
    return summary;
}

} // namespace nexus::module::backup
