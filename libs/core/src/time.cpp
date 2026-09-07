#include "nexus/core/time.hpp"

#include <array>
#include <ctime>

namespace nexus::core {

Timestamp now() noexcept {
    return Clock::now();
}

std::string to_iso8601(Timestamp tp) {
    const std::time_t t = Clock::to_time_t(tp);
    std::tm tm{};
#if defined(_WIN32)
    ::gmtime_s(&tm, &t);
#else
    ::gmtime_r(&t, &tm);
#endif
    std::array<char, 21> buf{};
    const std::size_t n = std::strftime(buf.data(), buf.size(), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buf.data(), n);
}

} // namespace nexus::core
