#include "nexus/system/system_provider.hpp"

// Placeholder for platforms without a provider yet. Linux/macOS implementations
// land in their own files alongside the Windows one.

namespace nexus::system {

std::unique_ptr<SystemProvider> make_system_provider() {
    return nullptr;
}

} // namespace nexus::system
