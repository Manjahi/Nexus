#include "nexus/fs/directory_watcher.hpp"

#include <atomic>
#include <string_view>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace nexus::fs {

namespace {

struct HandleCloser {
    void operator()(HANDLE h) const noexcept {
        if (h != nullptr && h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
        }
    }
};
using UniqueHandle = std::unique_ptr<void, HandleCloser>;

ChangeKind kind_from_action(DWORD action) {
    switch (action) {
        case FILE_ACTION_ADDED:
            return ChangeKind::Created;
        case FILE_ACTION_REMOVED:
            return ChangeKind::Removed;
        case FILE_ACTION_RENAMED_OLD_NAME:
            return ChangeKind::RenamedFrom;
        case FILE_ACTION_RENAMED_NEW_NAME:
            return ChangeKind::RenamedTo;
        case FILE_ACTION_MODIFIED:
        default:
            return ChangeKind::Modified;
    }
}

} // namespace

struct DirectoryWatcher::Impl {
    std::filesystem::path root;
    ChangeCallback on_change;
    std::chrono::milliseconds debounce;
    std::size_t buffer_bytes;

    UniqueHandle dir_handle;
    UniqueHandle stop_event;
    UniqueHandle data_event;
    std::vector<BYTE> buffer;
    OVERLAPPED overlapped{};
    std::thread worker;
    std::atomic<bool> active{false};

    Impl(std::filesystem::path root_, ChangeCallback on_change_,
         std::chrono::milliseconds debounce_, std::size_t buffer_bytes_)
        : root(std::move(root_)), on_change(std::move(on_change_)), debounce(debounce_),
          buffer_bytes(buffer_bytes_) {}

    /// Issues (or re-issues) the async read. Must only be called when no
    /// read is currently outstanding. Returns false if the OS call itself
    /// failed synchronously (not ERROR_IO_PENDING) - caller should back off
    /// and retry rather than treat this as fatal, since it can happen
    /// transiently (e.g. the watched directory briefly locked).
    bool arm() {
        ResetEvent(data_event.get());
        DWORD unused_bytes = 0;
        const BOOL issued = ReadDirectoryChangesW(
            dir_handle.get(), buffer.data(), static_cast<DWORD>(buffer.size()),
            /*bWatchSubtree=*/TRUE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE,
            &unused_bytes, &overlapped, nullptr);
        return issued || GetLastError() == ERROR_IO_PENDING;
    }

    void run() {
        std::vector<FileChange> pending;
        HANDLE wait_handles[2] = {stop_event.get(), data_event.get()};
        bool armed = true; // start() already armed the first read

        while (true) {
            if (!armed) {
                if (!arm()) {
                    if (WaitForSingleObject(stop_event.get(), 200) == WAIT_OBJECT_0) {
                        break;
                    }
                    continue;
                }
                armed = true;
            }

            const DWORD wait_ms = pending.empty() ? INFINITE : static_cast<DWORD>(debounce.count());
            const DWORD result = WaitForMultipleObjects(2, wait_handles, FALSE, wait_ms);

            if (result == WAIT_OBJECT_0) {
                // Stop requested: cancel the in-flight read and drain its
                // completion so the OVERLAPPED/handles aren't left with a
                // dangling async op when we close them.
                CancelIoEx(dir_handle.get(), &overlapped);
                DWORD drained = 0;
                GetOverlappedResult(dir_handle.get(), &overlapped, &drained, TRUE);
                break;
            }
            if (result == WAIT_OBJECT_0 + 1) {
                DWORD transferred = 0;
                const BOOL ok =
                    GetOverlappedResult(dir_handle.get(), &overlapped, &transferred, FALSE);
                armed = false; // this read is done either way - must re-arm
                if (!ok) {
                    const DWORD err = GetLastError();
                    if (err == ERROR_OPERATION_ABORTED) {
                        break; // we're stopping
                    }
                    if (err == ERROR_NOTIFY_ENUM_DIR) {
                        pending.push_back({ChangeKind::Overflowed, {}});
                    } else {
                        break; // handle no longer usable
                    }
                } else if (transferred == 0) {
                    // A zero-length completion also signals overflow.
                    pending.push_back({ChangeKind::Overflowed, {}});
                } else {
                    std::size_t offset = 0;
                    while (offset < transferred) {
                        const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(
                            buffer.data() + offset);
                        const std::wstring_view name(info->FileName,
                                                     info->FileNameLength / sizeof(wchar_t));
                        pending.push_back(
                            {kind_from_action(info->Action), root / std::filesystem::path(name)});
                        if (info->NextEntryOffset == 0) {
                            break;
                        }
                        offset += info->NextEntryOffset;
                    }
                }
                continue;
            }
            if (result == WAIT_TIMEOUT) {
                if (!pending.empty()) {
                    on_change(pending);
                    pending.clear();
                }
                continue;
            }
            break; // WAIT_FAILED or WAIT_ABANDONED - give up
        }
    }
};

DirectoryWatcher::DirectoryWatcher(std::filesystem::path root, ChangeCallback on_change,
                                   std::chrono::milliseconds debounce, std::size_t buffer_bytes)
    : impl_(std::make_unique<Impl>(std::move(root), std::move(on_change), debounce, buffer_bytes)) {
}

DirectoryWatcher::~DirectoryWatcher() {
    stop();
}

bool DirectoryWatcher::start() {
    if (impl_->active.load(std::memory_order_relaxed)) {
        return true;
    }
    if (!std::filesystem::is_directory(impl_->root)) {
        return false;
    }

    impl_->dir_handle.reset(
        CreateFileW(impl_->root.c_str(), FILE_LIST_DIRECTORY,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr));
    if (impl_->dir_handle.get() == INVALID_HANDLE_VALUE) {
        impl_->dir_handle.reset();
        return false;
    }

    impl_->stop_event.reset(CreateEventW(nullptr, /*manualReset=*/TRUE, FALSE, nullptr));
    impl_->data_event.reset(CreateEventW(nullptr, /*manualReset=*/TRUE, FALSE, nullptr));
    if (impl_->stop_event.get() == nullptr || impl_->data_event.get() == nullptr) {
        impl_->dir_handle.reset();
        impl_->stop_event.reset();
        impl_->data_event.reset();
        return false;
    }

    impl_->buffer.assign(impl_->buffer_bytes, BYTE{0});
    impl_->overlapped = OVERLAPPED{};
    impl_->overlapped.hEvent = impl_->data_event.get();

    // Arm the first read synchronously, on THIS thread, before returning -
    // otherwise a caller that creates a file immediately after start()
    // returns could race the worker thread's first ReadDirectoryChangesW
    // call and lose that change.
    if (!impl_->arm()) {
        impl_->dir_handle.reset();
        impl_->stop_event.reset();
        impl_->data_event.reset();
        return false;
    }

    impl_->active.store(true, std::memory_order_relaxed);
    impl_->worker = std::thread([this] { impl_->run(); });
    return true;
}

void DirectoryWatcher::stop() {
    if (!impl_->active.load(std::memory_order_relaxed)) {
        return;
    }
    impl_->active.store(false, std::memory_order_relaxed);
    SetEvent(impl_->stop_event.get());
    if (impl_->worker.joinable()) {
        impl_->worker.join();
    }
    impl_->dir_handle.reset();
    impl_->stop_event.reset();
    impl_->data_event.reset();
}

bool DirectoryWatcher::active() const noexcept {
    return impl_->active.load(std::memory_order_relaxed);
}

} // namespace nexus::fs
