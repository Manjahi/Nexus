#include "nexus/fs/directory_watcher.hpp"

namespace nexus::fs {

struct DirectoryWatcher::Impl {};

DirectoryWatcher::DirectoryWatcher(std::filesystem::path, ChangeCallback, std::chrono::milliseconds,
                                   std::size_t)
    : impl_(std::make_unique<Impl>()) {
}

DirectoryWatcher::~DirectoryWatcher() = default;

bool DirectoryWatcher::start() {
    return false;
}

void DirectoryWatcher::stop() {
}

bool DirectoryWatcher::active() const noexcept {
    return false;
}

} // namespace nexus::fs
