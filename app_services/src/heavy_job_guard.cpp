#include "nexus/services/heavy_job_guard.hpp"

#include <algorithm>

namespace nexus::services {

HeavyJobGuard::Lease::Lease(Lease&& other) noexcept
    : guard_(other.guard_), token_(other.token_) {
    other.guard_ = nullptr;
    other.token_ = 0;
}

HeavyJobGuard::Lease& HeavyJobGuard::Lease::operator=(Lease&& other) noexcept {
    if (this != &other) {
        if (guard_ != nullptr) {
            guard_->release(token_);
        }
        guard_ = other.guard_;
        token_ = other.token_;
        other.guard_ = nullptr;
        other.token_ = 0;
    }
    return *this;
}

HeavyJobGuard::Lease::~Lease() {
    if (guard_ != nullptr) {
        guard_->release(token_);
    }
}

std::vector<std::string> HeavyJobGuard::active() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> labels;
    labels.reserve(holders_.size());
    for (const auto& [token, label] : holders_) {
        labels.push_back(label);
    }
    return labels;
}

HeavyJobGuard::Lease HeavyJobGuard::acquire(std::string label) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::uint64_t token = next_token_++;
    holders_.emplace_back(token, std::move(label));
    return Lease(*this, token);
}

void HeavyJobGuard::release(std::uint64_t token) {
    std::lock_guard<std::mutex> lock(mutex_);
    holders_.erase(std::remove_if(holders_.begin(), holders_.end(),
                                  [token](const auto& entry) { return entry.first == token; }),
                   holders_.end());
}

} // namespace nexus::services
