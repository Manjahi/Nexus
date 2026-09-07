#include "nexus/jobs/cancellation.hpp"

namespace nexus::jobs {

OperationCancelled::OperationCancelled() : std::runtime_error("operation cancelled") {}

void CancellationToken::throw_if_cancelled() const {
    if (is_cancelled()) {
        throw OperationCancelled();
    }
}

} // namespace nexus::jobs
