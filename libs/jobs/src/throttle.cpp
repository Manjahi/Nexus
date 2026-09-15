#include "nexus/jobs/throttle.hpp"

#include <thread>

namespace nexus::jobs {

std::string_view to_string(ThrottleLevel level) noexcept {
    switch (level) {
        case ThrottleLevel::Unlimited:
            return "unlimited";
        case ThrottleLevel::High:
            return "high";
        case ThrottleLevel::Normal:
            return "normal";
        case ThrottleLevel::Low:
            return "low";
    }
    return "unlimited";
}

std::optional<ThrottleLevel> throttle_level_from_string(std::string_view text) noexcept {
    if (text == "unlimited") {
        return ThrottleLevel::Unlimited;
    }
    if (text == "high") {
        return ThrottleLevel::High;
    }
    if (text == "normal") {
        return ThrottleLevel::Normal;
    }
    if (text == "low") {
        return ThrottleLevel::Low;
    }
    return std::nullopt;
}

std::chrono::milliseconds Throttle::delay_for(ThrottleLevel level) noexcept {
    switch (level) {
        case ThrottleLevel::Unlimited:
            return std::chrono::milliseconds{0};
        case ThrottleLevel::High:
            return std::chrono::milliseconds{2};
        case ThrottleLevel::Normal:
            return std::chrono::milliseconds{8};
        case ThrottleLevel::Low:
            return std::chrono::milliseconds{25};
    }
    return std::chrono::milliseconds{0};
}

void Throttle::pace() const {
    const auto delay = delay_for(level_);
    if (delay.count() > 0) {
        std::this_thread::sleep_for(delay);
    }
}

} // namespace nexus::jobs
