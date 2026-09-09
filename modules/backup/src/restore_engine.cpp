#include "nexus/module/backup/restore_engine.hpp"

#include <algorithm>
#include <vector>

#include "nexus/hash/hash.hpp"
#include "nexus/module/backup/backup_repository.hpp"
#include "nexus/module/backup/object_store.hpp"

namespace nexus::module::backup {

namespace fs = std::filesystem;

namespace {
void emit(const Progress& progress, double fraction, std::string_view phase) {
    if (progress) {
        progress(std::clamp(fraction, 0.0, 1.0), phase);
    }
}
} // namespace

RestoreSummary RestoreEngine::restore(const nexus::core::Uuid& snapshot_id,
                                      const fs::path& target_dir, std::string_view only_path,
                                      const Progress& progress,
                                      const std::function<bool()>& cancelled) {
    RestoreSummary summary;

    std::vector<SnapshotFile> files = repo_->files_in(snapshot_id);
    if (!only_path.empty()) {
        std::erase_if(files, [&](const SnapshotFile& f) { return f.path != only_path; });
    }

    for (std::size_t i = 0; i < files.size(); ++i) {
        if (cancelled && cancelled()) {
            summary.cancelled = true;
            break;
        }
        const SnapshotFile& file = files[i];
        const auto digest = nexus::hash::digest_from_hex(file.digest);
        if (!digest) {
            ++summary.errors;
            continue;
        }
        const fs::path out = target_dir / fs::path(file.path);
        if (!store_->contains(*digest)) {
            ++summary.missing_blobs;
        } else if (store_->extract_to(*digest, out)) {
            ++summary.files_restored;
        } else {
            ++summary.errors;
        }
        if (!files.empty()) {
            emit(progress, static_cast<double>(i + 1) / static_cast<double>(files.size()),
                 "restoring");
        }
    }

    summary.restore_id = repo_->record_restore(
        snapshot_id, target_dir.generic_string(),
        summary.cancelled ? "cancelled" : (summary.ok() ? "completed" : "partial"),
        summary.files_restored);
    emit(progress, 1.0, "done");
    return summary;
}

} // namespace nexus::module::backup
