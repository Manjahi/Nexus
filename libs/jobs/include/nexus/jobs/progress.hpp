#pragma once

#include <functional>
#include <mutex>
#include <string>

namespace nexus::jobs {

/// A point-in-time progress snapshot. `fraction` is clamped to [0, 1].
struct Progress {
    double fraction = 0.0;
    std::string message;
};

/// Thread-safe progress accumulator. Each mutating call notifies the sink (if
/// any) with a fresh snapshot, invoked outside the internal lock.
class ProgressReporter {
public:
    using Sink = std::function<void(const Progress&)>;

    ProgressReporter() = default;
    explicit ProgressReporter(Sink sink) : sink_(std::move(sink)) {}

    void set_fraction(double fraction);
    void set_message(std::string message);
    void update(double fraction, std::string message);

    [[nodiscard]] Progress snapshot() const;

private:
    void notify(Progress copy) const;

    mutable std::mutex mutex_;
    Progress state_;
    Sink sink_;
};

} // namespace nexus::jobs
