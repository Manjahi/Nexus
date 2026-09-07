#include "nexus/jobs/progress.hpp"

#include <algorithm>
#include <utility>

namespace nexus::jobs {

namespace {

double clamp_fraction(double value) {
    return std::clamp(value, 0.0, 1.0);
}

} // namespace

void ProgressReporter::set_fraction(double fraction) {
    Progress copy;
    {
        std::scoped_lock lock(mutex_);
        state_.fraction = clamp_fraction(fraction);
        copy = state_;
    }
    notify(std::move(copy));
}

void ProgressReporter::set_message(std::string message) {
    Progress copy;
    {
        std::scoped_lock lock(mutex_);
        state_.message = std::move(message);
        copy = state_;
    }
    notify(std::move(copy));
}

void ProgressReporter::update(double fraction, std::string message) {
    Progress copy;
    {
        std::scoped_lock lock(mutex_);
        state_.fraction = clamp_fraction(fraction);
        state_.message = std::move(message);
        copy = state_;
    }
    notify(std::move(copy));
}

Progress ProgressReporter::snapshot() const {
    std::scoped_lock lock(mutex_);
    return state_;
}

void ProgressReporter::notify(Progress copy) const {
    Sink sink;
    {
        std::scoped_lock lock(mutex_);
        sink = sink_;
    }
    if (sink) {
        sink(copy);
    }
}

} // namespace nexus::jobs
