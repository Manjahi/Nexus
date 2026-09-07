#include "nexus/core/time.hpp"

#include <array>
#include <cstddef>
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

std::optional<Timestamp> from_iso8601(std::string_view text) {
    if (text.size() != 20) {
        return std::nullopt;
    }
    if (text[4] != '-' || text[7] != '-' || text[10] != 'T' || text[13] != ':' || text[16] != ':' ||
        text[19] != 'Z') {
        return std::nullopt;
    }

    const auto read = [&](std::size_t pos, std::size_t count, int& out) {
        int value = 0;
        for (std::size_t i = 0; i < count; ++i) {
            const char c = text[pos + i];
            if (c < '0' || c > '9') {
                return false;
            }
            value = value * 10 + (c - '0');
        }
        out = value;
        return true;
    };

    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    if (!read(0, 4, year) || !read(5, 2, month) || !read(8, 2, day) || !read(11, 2, hour) ||
        !read(14, 2, minute) || !read(17, 2, second)) {
        return std::nullopt;
    }
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 60) {
        return std::nullopt;
    }

    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = second;

#if defined(_WIN32)
    const std::time_t t = _mkgmtime(&tm);
#else
    const std::time_t t = timegm(&tm);
#endif
    if (t == static_cast<std::time_t>(-1)) {
        return std::nullopt;
    }
    return Clock::from_time_t(t);
}

} // namespace nexus::core
