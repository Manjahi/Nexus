#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nexus::db {

class Database;

/// Key/value access over the `app_settings` table. Requires the core schema
/// migration to have been applied.
class SettingsRepository {
public:
    explicit SettingsRepository(Database& db) noexcept : db_(&db) {}

    [[nodiscard]] std::optional<std::string> get(std::string_view key) const;
    [[nodiscard]] std::string get_or(std::string_view key, std::string_view fallback) const;

    /// Inserts or replaces `key`, refreshing `updated_at`.
    void set(std::string_view key, std::string_view value);

    void remove(std::string_view key);

    [[nodiscard]] std::vector<std::pair<std::string, std::string>> all() const;

private:
    Database* db_;
};

} // namespace nexus::db
