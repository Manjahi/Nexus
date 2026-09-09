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

} // namespace nexus::module::backup
