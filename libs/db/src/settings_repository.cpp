#include "nexus/db/settings_repository.hpp"

#include "nexus/db/database.hpp"
#include "nexus/db/statement.hpp"

#include "nexus/core/time.hpp"

namespace nexus::db {

std::optional<std::string> SettingsRepository::get(std::string_view key) const {
    Statement stmt = db_->prepare("SELECT value FROM app_settings WHERE key = ?");
    stmt.bind(1, key);
    if (!stmt.step()) {
        return std::nullopt;
    }
    return stmt.column_text(0);
}

std::string SettingsRepository::get_or(std::string_view key, std::string_view fallback) const {
    if (auto value = get(key)) {
        return *value;
    }
    return std::string(fallback);
}

void SettingsRepository::set(std::string_view key, std::string_view value) {
    Statement stmt = db_->prepare(
        "INSERT INTO app_settings (key, value, updated_at) VALUES (?, ?, ?) "
        "ON CONFLICT(key) DO UPDATE SET value = excluded.value, updated_at = excluded.updated_at");
    stmt.bind(1, key);
    stmt.bind(2, value);
    stmt.bind(3, nexus::core::to_iso8601(nexus::core::now()));
    stmt.step();
}

void SettingsRepository::remove(std::string_view key) {
    Statement stmt = db_->prepare("DELETE FROM app_settings WHERE key = ?");
    stmt.bind(1, key);
    stmt.step();
}

std::vector<std::pair<std::string, std::string>> SettingsRepository::all() const {
    Statement stmt = db_->prepare("SELECT key, value FROM app_settings ORDER BY key");
    std::vector<std::pair<std::string, std::string>> rows;
    while (stmt.step()) {
        rows.emplace_back(stmt.column_text(0), stmt.column_text(1));
    }
    return rows;
}

} // namespace nexus::db
