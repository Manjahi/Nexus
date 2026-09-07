#include "nexus/core/version.hpp"

#include <cstdio>

// Placeholder entry point. The vault is a separate security project built at
// Milestone 7 with its own threat model and review.
int main() {
    std::printf("nexuspc-vault %s - not implemented yet\n", nexus::core::version_string);
    return 0;
}
