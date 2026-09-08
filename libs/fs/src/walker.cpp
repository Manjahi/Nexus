#include "nexus/fs/walker.hpp"

#include <string>
#include <system_error>

namespace nexus::fs {

namespace fs = std::filesystem;

namespace {

std::string relative_key(const fs::path& path, const fs::path& root) {
    std::error_code ec;
    fs::path rel = fs::relative(path, root, ec);
    if (ec) {
        rel = path.filename();
    }
    return rel.generic_string();
}

} // namespace

WalkStats walk(const fs::path& root, const ExclusionRules& rules, const WalkOptions& options,
               const FileVisitor& on_file, const std::function<bool()>& cancelled) {
    WalkStats stats;

    std::error_code ec;
    if (!fs::is_directory(root, ec)) {
        ++stats.errors;
        return stats;
    }

    auto opts = fs::directory_options::skip_permission_denied;
    fs::recursive_directory_iterator it(root, opts, ec);
    if (ec) {
        ++stats.errors;
        return stats;
    }
    const fs::recursive_directory_iterator end;

    while (it != end) {
        if (cancelled && cancelled()) {
            stats.cancelled = true;
            break;
        }

        const fs::directory_entry& entry = *it;
        std::error_code entry_ec;

        const bool is_symlink = entry.is_symlink(entry_ec);
        const bool is_dir = entry.is_directory(entry_ec);

        if (is_dir) {
            ++stats.directories;
            const std::string name = entry.path().filename().generic_string();
            const std::string rel = relative_key(entry.path(), root);
            const bool at_depth_limit = options.max_depth >= 0 && it.depth() >= options.max_depth;
            if (rules.prunes_directory(name) || rules.excludes_path(rel) ||
                (is_symlink && !options.follow_symlinks) || at_depth_limit) {
                if (rules.prunes_directory(name) || rules.excludes_path(rel)) {
                    ++stats.excluded;
                }
                it.disable_recursion_pending();
            }
        } else if (entry.is_regular_file(entry_ec)) {
            ++stats.files;
            const std::string rel = relative_key(entry.path(), root);
            if (rules.excludes_path(rel)) {
                ++stats.excluded;
            } else {
                FileEntry file;
                file.path = entry.path();
                file.is_symlink = is_symlink;
                const auto size = entry.file_size(entry_ec);
                if (entry_ec) {
                    ++stats.errors;
                } else {
                    file.size = size;
                    stats.bytes += size;
                }
                file.last_write_time = entry.last_write_time(entry_ec);
                if (on_file) {
                    on_file(file);
                }
            }
        } else if (entry_ec) {
            ++stats.errors;
        }

        it.increment(ec);
        if (ec) {
            ++stats.errors;
            ec.clear();
            // Best effort: try to advance past the problem entry.
            it.increment(ec);
            if (ec) {
                break;
            }
        }
    }

    return stats;
}

} // namespace nexus::fs
