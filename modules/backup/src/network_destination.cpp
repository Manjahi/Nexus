#include "nexus/module/backup/network_destination.hpp"

#include <filesystem>
#include <system_error>

#ifdef _WIN32
#include <winerror.h>
#endif

namespace nexus::module::backup {

bool is_unc_destination(const std::string& destination) noexcept {
    return destination.rfind("\\\\", 0) == 0;
}

namespace {

#ifdef _WIN32
/// std::filesystem reports failures via std::system_category(), whose
/// value() on Windows is the raw Win32 error code - these are the ones a
/// user is actually likely to hit pointing a backup at a network share.
std::optional<std::string> describe_win32_network_error(int code) {
    switch (code) {
        case ERROR_BAD_NETPATH: // 53
        case ERROR_BAD_NET_NAME: // 67
        case ERROR_NETNAME_DELETED: // 64
            return "That network path doesn't exist or isn't shared - check the server "
                  "name and share name.";
        case ERROR_LOGON_FAILURE: // 1326
        case ERROR_ACCESS_DENIED: // 5
            return "Access was denied - this PC may not have permission to reach that "
                  "share (check credentials/mapped-drive login).";
        case ERROR_NETWORK_UNREACHABLE: // 1231
        case ERROR_NETWORK_BUSY: // 54
        case ERROR_UNEXP_NET_ERR: // 59
            return "The network is unreachable right now - check the connection and try "
                  "again.";
        case ERROR_SEM_TIMEOUT: // 121
            return "The network share didn't respond in time - it may be offline.";
        default:
            return std::nullopt;
    }
}
#endif

} // namespace

std::optional<std::string> check_destination_reachable(const std::string& destination) {
    const std::filesystem::path path(destination);
    std::error_code ec;
    if (std::filesystem::exists(path, ec) && !ec) {
        return std::nullopt;
    }
    if (!ec) {
        // exists() succeeded and reported "no" - not itself a network
        // error; for a UNC destination that most likely means the share
        // is reachable but this particular subfolder doesn't exist yet,
        // which BackupEngine creates on demand, so don't block on it.
        return is_unc_destination(destination)
                  ? std::nullopt
                  : std::make_optional<std::string>("That folder doesn't exist.");
    }

#ifdef _WIN32
    if (const auto mapped = describe_win32_network_error(ec.value())) {
        return mapped;
    }
#endif
    return ec.message();
}

} // namespace nexus::module::backup
