#pragma once

#include <chrono>
#include <functional>

namespace nexus::jobs {

/// Exponential backoff parameters. Attempt numbers are 1-based; the delay
/// applies *before* attempts 2..N.
struct RetryPolicy {
    int max_attempts = 3;
    std::chrono::milliseconds base_delay{100};
    double multiplier = 2.0;
    std::chrono::milliseconds max_delay{30'000};

    /// A policy that never retries.
    [[nodiscard]] static RetryPolicy none() {
        return {1, std::chrono::milliseconds::zero(), 1.0, std::chrono::milliseconds::zero()};
    }
};

/// Backoff delay before `attempt` (>= 1). Returns zero for the first attempt and
/// never exceeds `max_delay`.
[[nodiscard]] std::chrono::milliseconds delay_for_attempt(const RetryPolicy& policy, int attempt);

using SleepFn = std::function<void(std::chrono::milliseconds)>;

/// Invokes `action` until it returns without throwing or `max_attempts` is
/// reached. Between attempts, calls `sleep` with the backoff delay. On
/// exhaustion, rethrows the exception from the final attempt.
///
/// Returns the number of attempts made (>= 1).
int run_with_retry(const std::function<void()>& action, const RetryPolicy& policy,
                   const SleepFn& sleep);

/// Overload that sleeps with std::this_thread::sleep_for.
int run_with_retry(const std::function<void()>& action, const RetryPolicy& policy);

} // namespace nexus::jobs
