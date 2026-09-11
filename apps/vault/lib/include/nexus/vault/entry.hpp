#pragma once

#include <nlohmann/json_fwd.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "nexus/core/time.hpp"

namespace nexus::vault {

/// One vault record. Serialized as JSON, then the whole entry list is sealed
/// as a single AEAD blob (see vault_file.hpp) - this struct's shape never
/// touches disk in plaintext.
struct Entry {
    std::string id; ///< Uuid string; empty means "not yet assigned"
    std::string title;
    std::string username;
    std::string password;
    std::string url;
    std::string notes;
    std::vector<std::string> tags;
    nexus::core::Timestamp created_at{};
    nexus::core::Timestamp updated_at{};
};

/// Everything about an entry except its secret fields (password, notes) -
/// what VaultStore::list() returns so a "browse your vault" UI flow never
/// pulls plaintext secrets across the IPC pipe until the user asks for one.
struct EntrySummary {
    std::string id;
    std::string title;
    std::string username;
    std::vector<std::string> tags;
    nexus::core::Timestamp updated_at{};
};

enum class HealthIssue { Weak, Reused, Old };

struct HealthFinding {
    std::string entry_id;
    std::string title;
    HealthIssue issue = HealthIssue::Weak;
};

/// Best-effort overwrite of an entry's secret-bearing fields before it's
/// destroyed. std::string gives no destruction-time zeroing guarantee (small-
/// string-optimization buffers, past reallocations, and moves may all have
/// left copies elsewhere in the heap) - this reduces the window without
/// claiming to close it entirely. See the vault threat model's review
/// checklist for what a stronger guarantee would require.
void secure_clear(Entry& entry) noexcept;

/// One entry as JSON - shared by the vault file format (a JSON array of
/// these, sealed as one AEAD payload) and the vault process's IPC wire
/// protocol (one of these per get/put message), so the field mapping is
/// defined in exactly one place.
[[nodiscard]] nlohmann::json to_json(const Entry& entry);
[[nodiscard]] std::optional<Entry> entry_from_json(const nlohmann::json& j);

/// The plaintext that gets sealed as the vault's AEAD payload (see
/// vault_file.hpp) - a JSON array of entries.
[[nodiscard]] std::string serialize_entries(const std::vector<Entry>& entries);
[[nodiscard]] std::optional<std::vector<Entry>> parse_entries(std::string_view json);

} // namespace nexus::vault
