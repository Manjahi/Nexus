#pragma once

#include <nlohmann/json_fwd.hpp>

namespace nexus::vault {

class VaultStore;

/// Handles one decoded JSON request against `store` and returns the JSON
/// response. A pure function of (store, request) - no IPC or threading here,
/// so the whole wire protocol is testable without a pipe. See ADR-0003 for
/// the verb table (status, create, unlock, lock, list, get, put, delete,
/// generate_password, health).
[[nodiscard]] nlohmann::json handle_request(VaultStore& store, const nlohmann::json& request);

} // namespace nexus::vault
