#include "nexus/fs/directory_watcher.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;
using nexus::fs::ChangeKind;
using nexus::fs::DirectoryWatcher;
using nexus::fs::FileChange;

namespace {

struct TempWatchDir {
    fs::path root;

    TempWatchDir() {
        const auto tag = std::chrono::steady_clock::now().time_since_epoch().count();
        root = fs::temp_directory_path() / ("nexuspc_watch_" + std::to_string(tag));
        fs::create_directories(root);
    }
    ~TempWatchDir() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
};

void write(const fs::path& p, std::string_view content) {
    std::ofstream out(p, std::ios::binary);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

/// Accumulates every change the watcher's background thread reports, and
/// lets the test thread block until a predicate over that (growing) list
/// becomes true - real filesystem notifications arrive asynchronously, so
/// tests can't just call the watcher and immediately assert.
class ChangeCollector {
public:
    void operator()(const std::vector<FileChange>& changes) {
        std::lock_guard<std::mutex> lock(mutex_);
        all_.insert(all_.end(), changes.begin(), changes.end());
        cv_.notify_all();
    }

    [[nodiscard]] bool wait_until(std::chrono::milliseconds timeout,
                                  const std::function<bool(const std::vector<FileChange>&)>& pred) {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, timeout, [&] { return pred(all_); });
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<FileChange> all_;
};

bool has_change(const std::vector<FileChange>& changes, ChangeKind kind, const fs::path& path) {
    return std::any_of(changes.begin(), changes.end(), [&](const FileChange& c) {
        return c.kind == kind && c.path == path;
    });
}

bool has_kind(const std::vector<FileChange>& changes, ChangeKind kind) {
    return std::any_of(changes.begin(), changes.end(),
                       [&](const FileChange& c) { return c.kind == kind; });
}

constexpr std::chrono::milliseconds kWaitTimeout{5000};

} // namespace

TEST_CASE("start() fails for a directory that doesn't exist", "[fs][watcher]") {
    ChangeCollector collector;
    DirectoryWatcher watcher(fs::temp_directory_path() / "nexuspc_watch_does_not_exist",
                             std::ref(collector));
    REQUIRE_FALSE(watcher.start());
    REQUIRE_FALSE(watcher.active());
}

TEST_CASE("reports a created file", "[fs][watcher]") {
    TempWatchDir dir;
    ChangeCollector collector;
    DirectoryWatcher watcher(dir.root, std::ref(collector), std::chrono::milliseconds{200});
    REQUIRE(watcher.start());
    REQUIRE(watcher.active());

    const fs::path target = dir.root / "created.txt";
    write(target, "hello");

    const bool found = collector.wait_until(kWaitTimeout, [&](const auto& changes) {
        return has_change(changes, ChangeKind::Created, target);
    });
    REQUIRE(found);

    watcher.stop();
    REQUIRE_FALSE(watcher.active());
}

TEST_CASE("reports a modified file", "[fs][watcher]") {
    TempWatchDir dir;
    const fs::path target = dir.root / "existing.txt";
    write(target, "v1");

    ChangeCollector collector;
    DirectoryWatcher watcher(dir.root, std::ref(collector), std::chrono::milliseconds{200});
    REQUIRE(watcher.start());

    write(target, "v2, a bit longer so the size change is unambiguous");

    const bool found = collector.wait_until(kWaitTimeout, [&](const auto& changes) {
        return has_change(changes, ChangeKind::Modified, target);
    });
    REQUIRE(found);

    watcher.stop();
}

TEST_CASE("reports a removed file", "[fs][watcher]") {
    TempWatchDir dir;
    const fs::path target = dir.root / "doomed.txt";
    write(target, "goodbye");

    ChangeCollector collector;
    DirectoryWatcher watcher(dir.root, std::ref(collector), std::chrono::milliseconds{200});
    REQUIRE(watcher.start());

    fs::remove(target);

    const bool found = collector.wait_until(kWaitTimeout, [&](const auto& changes) {
        return has_change(changes, ChangeKind::Removed, target);
    });
    REQUIRE(found);

    watcher.stop();
}

TEST_CASE("reports a rename as a RenamedFrom/RenamedTo pair", "[fs][watcher]") {
    TempWatchDir dir;
    const fs::path from = dir.root / "old_name.txt";
    const fs::path to = dir.root / "new_name.txt";
    write(from, "content");

    ChangeCollector collector;
    DirectoryWatcher watcher(dir.root, std::ref(collector), std::chrono::milliseconds{200});
    REQUIRE(watcher.start());

    std::error_code ec;
    fs::rename(from, to, ec);
    REQUIRE_FALSE(ec);

    const bool found = collector.wait_until(kWaitTimeout, [&](const auto& changes) {
        return has_change(changes, ChangeKind::RenamedFrom, from) &&
              has_change(changes, ChangeKind::RenamedTo, to);
    });
    REQUIRE(found);

    watcher.stop();
}

// The real overflow-handling path (ERROR_NOTIFY_ENUM_DIR / a zero-length
// completion) is the single riskiest part of this watcher - a buffer too
// small to hold even one real change record forces it deterministically,
// instead of relying on a real-world burst large enough to overflow the
// production-sized (64 KiB) buffer, which would be slow and flaky here.
TEST_CASE("a too-small buffer reports Overflowed rather than hanging or crashing",
         "[fs][watcher]") {
    TempWatchDir dir;
    ChangeCollector collector;
    DirectoryWatcher watcher(dir.root, std::ref(collector), std::chrono::milliseconds{200},
                             /*buffer_bytes=*/16);
    REQUIRE(watcher.start());

    write(dir.root / "trigger.txt", "x");

    const bool found = collector.wait_until(
        kWaitTimeout, [&](const auto& changes) { return has_kind(changes, ChangeKind::Overflowed); });
    REQUIRE(found);

    watcher.stop();
}

TEST_CASE("stop() is idempotent and safe before start()", "[fs][watcher]") {
    TempWatchDir dir;
    ChangeCollector collector;
    DirectoryWatcher watcher(dir.root, std::ref(collector));
    watcher.stop(); // never started - must not crash
    REQUIRE(watcher.start());
    watcher.stop();
    watcher.stop(); // already stopped - must not crash or double-join
    REQUIRE_FALSE(watcher.active());
}
