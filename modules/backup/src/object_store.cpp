#include "nexus/module/backup/object_store.hpp"

#include <chrono>
#include <string>
#include <system_error>

namespace nexus::module::backup {

namespace fs = std::filesystem;

namespace {

std::string tmp_name() {
    return "tmp-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}

} // namespace

fs::path ObjectStore::path_for(const nexus::hash::Digest& digest) const {
    const std::string hex = nexus::hash::to_hex(digest);
    return root_ / hex.substr(0, 2) / hex;
}

bool ObjectStore::contains(const nexus::hash::Digest& digest) const {
    std::error_code ec;
    return fs::exists(path_for(digest), ec) && !ec;
}

std::optional<ObjectStore::PutResult> ObjectStore::put_file(const fs::path& source) {
    const auto digest = nexus::hash::hash_file(source);
    if (!digest) {
        return std::nullopt;
    }

    std::error_code ec;
    const std::uint64_t size = fs::file_size(source, ec);
    if (ec) {
        return std::nullopt;
    }

    PutResult result;
    result.digest = *digest;
    result.size = size;

    const fs::path target = path_for(*digest);
    if (fs::exists(target, ec)) {
        result.was_new = false;
        return result;
    }

    fs::create_directories(target.parent_path(), ec);
    const fs::path staging = target.parent_path() / tmp_name();
    fs::copy_file(source, staging, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        return std::nullopt;
    }
    fs::rename(staging, target, ec);
    if (ec) {
        // Someone else may have created it first; if so that's fine.
        fs::remove(staging, ec);
        if (!fs::exists(target)) {
            return std::nullopt;
        }
    }

    result.was_new = true;
    return result;
}

bool ObjectStore::extract_to(const nexus::hash::Digest& digest, const fs::path& destination) const {
    const fs::path blob = path_for(digest);
    std::error_code ec;
    if (!fs::exists(blob, ec)) {
        return false;
    }
    fs::create_directories(destination.parent_path(), ec);
    fs::copy_file(blob, destination, fs::copy_options::overwrite_existing, ec);
    return !ec;
}

bool ObjectStore::verify(const nexus::hash::Digest& digest) const {
    const auto actual = nexus::hash::hash_file(path_for(digest));
    return actual.has_value() && *actual == digest;
}

ObjectStore::GcResult ObjectStore::collect_garbage(const std::unordered_set<std::string>& keep) const {
    GcResult result;
    std::error_code ec;
    if (!fs::exists(root_, ec) || ec) {
        return result;
    }

    for (fs::recursive_directory_iterator it(
             root_, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        std::error_code is_file_ec;
        if (!it->is_regular_file(is_file_ec) || is_file_ec) {
            continue;
        }
        if (keep.contains(it->path().filename().string())) {
            continue;
        }
        std::error_code size_ec;
        const auto size = fs::file_size(it->path(), size_ec);
        std::error_code remove_ec;
        if (fs::remove(it->path(), remove_ec) && !remove_ec) {
            ++result.blobs_removed;
            result.bytes_reclaimed += size_ec ? 0 : static_cast<std::uint64_t>(size);
        }
    }

    // Sweep now-empty two-level shard directories left behind by the removals above.
    for (fs::directory_iterator it(root_, ec); !ec && it != fs::directory_iterator();
         it.increment(ec)) {
        std::error_code dir_ec;
        std::error_code empty_ec;
        if (it->is_directory(dir_ec) && !dir_ec && fs::is_empty(it->path(), empty_ec) &&
            !empty_ec) {
            std::error_code rm_ec;
            fs::remove(it->path(), rm_ec);
        }
    }

    return result;
}

} // namespace nexus::module::backup
