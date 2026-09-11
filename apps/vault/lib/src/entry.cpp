#include "nexus/vault/entry.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>

namespace nexus::vault {

namespace {

void zero_string(std::string& s) noexcept {
    if (!s.empty()) {
        // data() is non-const and contiguous for std::string since C++17.
        std::memset(s.data(), 0, s.size());
    }
    s.clear();
    s.shrink_to_fit();
}

} // namespace

void secure_clear(Entry& entry) noexcept {
    zero_string(entry.id);
    zero_string(entry.title);
    zero_string(entry.username);
    zero_string(entry.password);
    zero_string(entry.url);
    zero_string(entry.notes);
    for (std::string& tag : entry.tags) {
        zero_string(tag);
    }
    entry.tags.clear();
    entry.tags.shrink_to_fit();
}

nlohmann::json to_json(const Entry& e) {
    return {
        {"id", e.id},
        {"title", e.title},
        {"username", e.username},
        {"password", e.password},
        {"url", e.url},
        {"notes", e.notes},
        {"tags", e.tags},
        {"created_at", nexus::core::to_iso8601(e.created_at)},
        {"updated_at", nexus::core::to_iso8601(e.updated_at)},
    };
}

std::optional<Entry> entry_from_json(const nlohmann::json& j) {
    if (!j.is_object()) {
        return std::nullopt;
    }
    Entry e;
    try {
        e.id = j.at("id").get<std::string>();
        e.title = j.value("title", std::string{});
        e.username = j.value("username", std::string{});
        e.password = j.value("password", std::string{});
        e.url = j.value("url", std::string{});
        e.notes = j.value("notes", std::string{});
        e.tags = j.value("tags", std::vector<std::string>{});
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
    if (const auto created = nexus::core::from_iso8601(j.value("created_at", std::string{}))) {
        e.created_at = *created;
    }
    if (const auto updated = nexus::core::from_iso8601(j.value("updated_at", std::string{}))) {
        e.updated_at = *updated;
    }
    return e;
}

std::string serialize_entries(const std::vector<Entry>& entries) {
    nlohmann::json array = nlohmann::json::array();
    for (const Entry& e : entries) {
        array.push_back(to_json(e));
    }
    return array.dump();
}

std::optional<std::vector<Entry>> parse_entries(std::string_view json) {
    nlohmann::json parsed;
    try {
        parsed = nlohmann::json::parse(json);
    } catch (const nlohmann::json::parse_error&) {
        return std::nullopt;
    }
    if (!parsed.is_array()) {
        return std::nullopt;
    }

    std::vector<Entry> out;
    out.reserve(parsed.size());
    for (const auto& item : parsed) {
        auto entry = entry_from_json(item);
        if (!entry) {
            return std::nullopt;
        }
        out.push_back(std::move(*entry));
    }
    return out;
}

} // namespace nexus::vault
