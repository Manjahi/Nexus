#include "nexus/module/storage/duplicate_scanner.hpp"

#include "nexus/fs/exclusion_rules.hpp"
#include "nexus/fs/walker.hpp"
#include "nexus/hash/hash.hpp"
#include "nexus/module/storage/storage_repository.hpp"

#include <algorithm>
#include <map>
#include <unordered_map>
#include <utility>

namespace nexus::module::storage {

namespace {

struct Candidate {
    std::filesystem::path path;
    std::uint64_t size = 0;
};

bool is_cancelled(const std::function<bool()>& cancelled) {
    return cancelled && cancelled();
}

void report(const ScanProgress& progress, double fraction, std::string_view phase) {
    if (progress) {
        progress(std::clamp(fraction, 0.0, 1.0), phase);
    }
}

} // namespace

std::uint64_t ScanSummary::reclaimable_bytes() const noexcept {
    std::uint64_t total = 0;
    for (const DuplicateGroup& g : groups) {
        total += g.reclaimable_bytes();
    }
    return total;
}

std::uint64_t ScanSummary::duplicate_file_count() const noexcept {
    std::uint64_t total = 0;
    for (const DuplicateGroup& g : groups) {
        total += g.files.size();
    }
    return total;
}

ScanSummary DuplicateScanner::scan(const std::filesystem::path& root,
                                   const nexus::fs::ExclusionRules& rules,
                                   const ScanOptions& options, const ScanProgress& progress,
                                   const std::function<bool()>& cancelled,
                                   const std::function<void()>& throttle) {
    ScanSummary summary;

    if (repo_ != nullptr && options.persist) {
        summary.scan_id = repo_->begin_scan(root.generic_string());
    }

    // Phase 1: walk and bucket by size.
    report(progress, 0.0, "walking");
    std::unordered_map<std::uint64_t, std::vector<Candidate>> by_size;
    const nexus::fs::WalkStats walk_stats = nexus::fs::walk(
        root, rules, {},
        [&](const nexus::fs::FileEntry& entry) {
            if (entry.size >= options.min_file_size) {
                by_size[entry.size].push_back({entry.path, entry.size});
            }
        },
        cancelled);
    summary.files_seen = walk_stats.files;
    summary.bytes_seen = walk_stats.bytes;
    summary.cancelled = walk_stats.cancelled;

    if (summary.cancelled) {
        if (repo_ != nullptr && options.persist) {
            repo_->finish_scan(summary.scan_id, summary, "cancelled");
        }
        return summary;
    }

    // Only sizes shared by >= 2 files can contain duplicates.
    std::vector<std::vector<Candidate>> size_groups;
    std::uint64_t candidates = 0;
    for (auto& [size, files] : by_size) {
        if (files.size() >= 2) {
            candidates += files.size();
            size_groups.push_back(std::move(files));
        }
    }

    // Phase 2: partial hash to split large same-size groups cheaply.
    report(progress, 0.3, "hashing");
    std::vector<std::vector<Candidate>> refined;
    for (auto& group : size_groups) {
        if (is_cancelled(cancelled)) {
            summary.cancelled = true;
            break;
        }
        const std::uint64_t size = group.front().size;
        if (group.size() <= 2 || size <= options.partial_hash_bytes) {
            refined.push_back(std::move(group));
            continue;
        }
        std::map<nexus::hash::Digest, std::vector<Candidate>> by_prefix;
        for (auto& c : group) {
            const auto digest = nexus::hash::hash_file_prefix(c.path, options.partial_hash_bytes);
            ++summary.files_hashed;
            if (throttle) {
                throttle();
            }
            if (digest) {
                by_prefix[*digest].push_back(std::move(c));
            }
        }
        for (auto& [digest, files] : by_prefix) {
            if (files.size() >= 2) {
                refined.push_back(std::move(files));
            }
        }
    }

    // Phase 3: full hash and final grouping.
    std::uint64_t hashed = 0;
    for (auto& group : refined) {
        if (is_cancelled(cancelled)) {
            summary.cancelled = true;
            break;
        }
        std::map<nexus::hash::Digest, std::vector<std::filesystem::path>> by_full;
        for (const Candidate& c : group) {
            const auto digest = nexus::hash::hash_file(c.path);
            ++summary.files_hashed;
            ++hashed;
            if (throttle) {
                throttle();
            }
            if (candidates > 0) {
                report(progress,
                       0.3 + 0.6 * (static_cast<double>(hashed) / static_cast<double>(candidates)),
                       "hashing");
            }
            if (digest) {
                by_full[*digest].push_back(c.path);
            }
        }
        for (auto& [digest, files] : by_full) {
            if (files.size() >= 2) {
                DuplicateGroup dup;
                dup.digest = nexus::hash::to_hex(digest);
                dup.file_size = group.front().size;
                std::sort(files.begin(), files.end());
                dup.files = std::move(files);
                summary.groups.push_back(std::move(dup));
            }
        }
    }

    std::sort(summary.groups.begin(), summary.groups.end(),
              [](const DuplicateGroup& a, const DuplicateGroup& b) {
                  return a.reclaimable_bytes() > b.reclaimable_bytes();
              });

    // Phase 4: persist.
    report(progress, 0.95, "saving");
    if (repo_ != nullptr && options.persist) {
        for (const DuplicateGroup& group : summary.groups) {
            repo_->add_group(summary.scan_id, group);
        }
        repo_->finish_scan(summary.scan_id, summary, summary.cancelled ? "cancelled" : "completed");
    }

    report(progress, 1.0, "done");
    return summary;
}

} // namespace nexus::module::storage
