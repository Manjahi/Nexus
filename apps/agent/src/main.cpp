#include "nexus/core/time.hpp"
#include "nexus/core/version.hpp"

#include <cstdio>

// Placeholder entry point. The agent gains real responsibilities (hardware
// sampling, connectivity probes, scheduled jobs) at Milestone 2.
int main() {
    std::printf("nexuspc-agent %s - not implemented yet (%s)\n", nexus::core::version_string,
                nexus::core::to_iso8601(nexus::core::now()).c_str());
    return 0;
}
