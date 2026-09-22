#pragma once

#include <cstdint>
#include <string>

namespace nexus::services::events {

/// Published after a storage duplicate scan completes with a non-zero
/// reclaimable total. The one place both modules/storage and modules/backup
/// can depend on without depending on each other directly (see
/// docs/IMPLEMENTATION_PLAN.md's "modules never call each other directly"
/// rule) - each side only needs to know this app_services type.
struct DuplicatesFoundEvent {
    std::uint64_t reclaimable_bytes = 0;
    std::string root;
};

/// Published by Connectivity whenever its "every target failing at once"
/// state changes (see Prober::classify_and_notify_total_outage) - an
/// edge-triggered signal, not one published on every probe tick.
struct ConnectivityStateEvent {
    bool internet_reachable = true;
};

} // namespace nexus::services::events
