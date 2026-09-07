#pragma once

#include <stdexcept>
#include <stop_token>

namespace nexus::jobs {

/// Thrown by CancellationToken::throw_if_cancelled().
class OperationCancelled : public std::runtime_error {
public:
    OperationCancelled();
};

/// Read-only view of a cancellation request. A default-constructed token is
/// never cancelled.
class CancellationToken {
public:
    CancellationToken() noexcept = default;
    explicit CancellationToken(std::stop_token token) noexcept : token_(std::move(token)) {}

    [[nodiscard]] bool is_cancelled() const noexcept {
        return token_.stop_possible() && token_.stop_requested();
    }

    /// Throws OperationCancelled if cancellation has been requested.
    void throw_if_cancelled() const;

    [[nodiscard]] std::stop_token raw() const noexcept { return token_; }

private:
    std::stop_token token_;
};

/// Owns a cancellation request and hands out tokens observing it.
class CancellationSource {
public:
    [[nodiscard]] CancellationToken token() const noexcept {
        return CancellationToken(source_.get_token());
    }
    void cancel() noexcept { source_.request_stop(); }
    [[nodiscard]] bool cancelled() const noexcept { return source_.stop_requested(); }

private:
    std::stop_source source_;
};

} // namespace nexus::jobs
