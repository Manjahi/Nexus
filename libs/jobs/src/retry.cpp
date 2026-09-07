#include "nexus/jobs/retry.hpp"

#include <algorithm>
#include <cmath>
#include <thread>

namespace nexus::jobs {

std::chrono::milliseconds delay_for_attempt(const RetryPolicy& policy, int attempt) {
    if (attempt <= 1) {
        return std::chrono::milliseconds::zero();
    }
    const double base = static_cast<double>(policy.base_delay.count());
    const double scaled = base * std::pow(policy.multiplier, attempt - 2);
    const double capped = std::min(scaled, static_cast<double>(policy.max_delay.count()));
    if (capped <= 0.0) {
        return std::chrono::milliseconds::zero();
    }
    return std::chrono::milliseconds(static_cast<std::chrono::milliseconds::rep>(capped));
}

int run_with_retry(const std::function<void()>& action, const RetryPolicy& policy,
                   const SleepFn& sleep) {
    const int attempts = std::max(1, policy.max_attempts);
    for (int attempt = 1;; ++attempt) {
        try {
            action();
            return attempt;
        } catch (...) {
            if (attempt >= attempts) {
                throw;
            }
            if (sleep) {
                sleep(delay_for_attempt(policy, attempt + 1));
            }
        }
    }
}

int run_with_retry(const std::function<void()>& action, const RetryPolicy& policy) {
    return run_with_retry(action, policy, [](std::chrono::milliseconds delay) {
        if (delay > std::chrono::milliseconds::zero()) {
            std::this_thread::sleep_for(delay);
        }
    });
}

} // namespace nexus::jobs
