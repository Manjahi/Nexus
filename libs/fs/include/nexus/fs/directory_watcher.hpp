#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

namespace nexus::fs {

enum class ChangeKind {
    Created,
    Modified,
    Removed,
    RenamedFrom,
    RenamedTo,
    /// The OS notification buffer overflowed (ERROR_NOTIFY_ENUM_DIR) - some
    /// changes were dropped between this notification and the last. `path`
    /// is empty; callers should treat this as "something changed somewhere
    /// under root, fall back to a full rescan" rather than trust the other
    /// changes in this batch to be complete.
    Overflowed,
};

struct FileChange {
    ChangeKind kind = ChangeKind::Modified;
    std::filesystem::path path;
};

using ChangeCallback = std::function<void(const std::vector<FileChange>&)>;

/// Watches `root` (recursively) for filesystem changes on a dedicated
/// background thread and, after `debounce` of quiet, invokes `on_change`
/// once with every change coalesced since the last invocation - so a save
/// (delete temp + rename) or a big copy doesn't fire the callback once per
/// individual event. `on_change` runs ON THE WATCHER'S BACKGROUND THREAD;
/// callers that need to touch UI state or another thread's data must
/// marshal it themselves (e.g. via nexus::jobs::ThreadPool::submit()).
///
/// Windows-only real implementation (ReadDirectoryChangesW, overlapped
/// I/O); on other platforms start() returns false and on_change is never
/// called - callers should treat that as "no watcher available, fall back
/// to periodic full scans," not a hard error.
class DirectoryWatcher {
public:
    /// `buffer_bytes` is the OS notification buffer size - exposed (rather
    /// than a fixed constant) so tests can force ERROR_NOTIFY_ENUM_DIR
    /// deterministically with a tiny buffer instead of racing a burst of
    /// real filesystem events against production's much larger default.
    explicit DirectoryWatcher(std::filesystem::path root, ChangeCallback on_change,
                              std::chrono::milliseconds debounce = std::chrono::milliseconds{750},
                              std::size_t buffer_bytes = 64 * 1024);
    ~DirectoryWatcher();

    DirectoryWatcher(const DirectoryWatcher&) = delete;
    DirectoryWatcher& operator=(const DirectoryWatcher&) = delete;

    /// Starts watching on a new background thread. Returns false (and
    /// starts nothing) if `root` doesn't exist or the OS watch could not be
    /// armed. A no-op (returns true) if already active.
    bool start();

    /// Stops watching and joins the background thread. Safe to call
    /// multiple times and implicit in the destructor. Does not flush any
    /// pending (not-yet-debounced) changes - a watcher stopped mid-debounce
    /// drops them, matching "the app is shutting down" semantics rather
    /// than "the caller wants a final callback."
    void stop();

    [[nodiscard]] bool active() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nexus::fs
