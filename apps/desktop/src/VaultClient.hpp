#pragma once

#include <nlohmann/json_fwd.hpp>

#include <mutex>
#include <optional>

#include "nexus/ipc/pipe.hpp"

namespace nexuspc::desktop {

/// Talks to nexuspc-vault over its named pipe, spawning the process (found
/// next to this one - see apps/desktop/CMakeLists.txt's post-build copy) the
/// first time it isn't already listening. One shared instance for the whole
/// MainWindow; every call blocks on pipe I/O and, for unlock/create, on the
/// vault's own Argon2id derivation - callers should run request() on the
/// thread pool, exactly like every other slow operation in this app.
class VaultClient {
public:
    VaultClient() = default;

    /// Sends `body` (must include a "verb" field - see ADR-0003's verb
    /// table) and returns the parsed response. A transport failure (vault
    /// unreachable, connection lost, malformed reply) comes back as
    /// {"ok":false,"error":"..."}, indistinguishable in shape from an
    /// application-level error the vault itself returned.
    [[nodiscard]] nlohmann::json request(nlohmann::json body);

private:
    [[nodiscard]] bool ensure_connected();

    std::mutex mutex_;
    std::optional<nexus::ipc::PipeConnection> connection_;
    bool spawn_attempted_ = false;
};

} // namespace nexuspc::desktop
