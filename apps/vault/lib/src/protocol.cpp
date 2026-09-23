#include "nexus/vault/protocol.hpp"

#include <nlohmann/json.hpp>

#include <string>

#include "nexus/core/time.hpp"
#include "nexus/crypto/crypto.hpp"
#include "nexus/vault/entry.hpp"
#include "nexus/vault/vault_store.hpp"

namespace nexus::vault {

namespace {

nlohmann::json ok_response() { return {{"ok", true}}; }

nlohmann::json error_response(std::string message) {
    return {{"ok", false}, {"error", std::move(message)}};
}

std::string_view issue_to_string(HealthIssue issue) {
    switch (issue) {
        case HealthIssue::Weak:
            return "weak";
        case HealthIssue::Reused:
            return "reused";
        case HealthIssue::Old:
            return "old";
    }
    return "weak";
}

nlohmann::json summary_to_json(const EntrySummary& s) {
    return {
        {"id", s.id},
        {"kind", std::string(to_string(s.kind))},
        {"title", s.title},
        {"username", s.username},
        {"tags", s.tags},
        {"updated_at", nexus::core::to_iso8601(s.updated_at)},
    };
}

nlohmann::json handle_status(VaultStore& store) {
    auto response = ok_response();
    response["locked"] = store.locked();
    response["entry_count"] = store.entry_count();
    response["vault_exists"] = store.vault_exists();
    return response;
}

nlohmann::json handle_create(VaultStore& store, const nlohmann::json& request) {
    const std::string password = request.value("master_password", std::string{});
    if (password.empty()) {
        return error_response("master_password required");
    }
    if (!store.create(password)) {
        return error_response("create failed (a vault may already exist at this path)");
    }
    return ok_response();
}

nlohmann::json handle_unlock(VaultStore& store, const nlohmann::json& request) {
    const std::string password = request.value("master_password", std::string{});
    if (!store.unlock(password)) {
        return error_response("wrong password or no vault at this path");
    }
    return ok_response();
}

nlohmann::json handle_list(VaultStore& store) {
    if (store.locked()) {
        return error_response("locked");
    }
    nlohmann::json entries = nlohmann::json::array();
    for (const EntrySummary& summary : store.list()) {
        entries.push_back(summary_to_json(summary));
    }
    auto response = ok_response();
    response["entries"] = std::move(entries);
    return response;
}

nlohmann::json handle_get(VaultStore& store, const nlohmann::json& request) {
    if (store.locked()) {
        return error_response("locked");
    }
    const std::string id = request.value("id", std::string{});
    const auto entry = store.get(id);
    if (!entry) {
        return error_response("not found");
    }
    auto response = ok_response();
    response["entry"] = to_json(*entry);
    return response;
}

nlohmann::json handle_put(VaultStore& store, const nlohmann::json& request) {
    if (store.locked()) {
        return error_response("locked");
    }
    if (!request.contains("entry")) {
        return error_response("entry required");
    }
    auto entry = entry_from_json(request.at("entry"));
    if (!entry) {
        return error_response("malformed entry");
    }
    const auto id = store.put(*entry);
    if (!id) {
        return error_response("save failed (unknown id, or write error)");
    }
    auto response = ok_response();
    response["id"] = *id;
    return response;
}

nlohmann::json handle_delete(VaultStore& store, const nlohmann::json& request) {
    if (store.locked()) {
        return error_response("locked");
    }
    const std::string id = request.value("id", std::string{});
    if (!store.remove(id)) {
        return error_response("not found");
    }
    return ok_response();
}

nlohmann::json handle_generate_password(const nlohmann::json& request) {
    nexus::crypto::PasswordPolicy policy;
    policy.length = request.value("length", policy.length);
    policy.lowercase = request.value("lowercase", policy.lowercase);
    policy.uppercase = request.value("uppercase", policy.uppercase);
    policy.digits = request.value("digits", policy.digits);
    policy.symbols = request.value("symbols", policy.symbols);

    const std::string password = nexus::crypto::generate_password(policy);
    if (password.empty()) {
        return error_response("invalid policy (zero length, or every character class disabled)");
    }
    auto response = ok_response();
    response["password"] = password;
    return response;
}

nlohmann::json handle_export(VaultStore& store, const nlohmann::json& request) {
    if (store.locked()) {
        return error_response("locked");
    }
    const std::string destination = request.value("destination", std::string{});
    if (destination.empty()) {
        return error_response("destination required");
    }
    if (!store.export_to(destination)) {
        return error_response(
            "export failed (a file already exists at that path, or the write failed)");
    }
    return ok_response();
}

nlohmann::json handle_health(VaultStore& store) {
    if (store.locked()) {
        return error_response("locked");
    }
    nlohmann::json findings = nlohmann::json::array();
    for (const HealthFinding& finding : store.health()) {
        findings.push_back({
            {"entry_id", finding.entry_id},
            {"title", finding.title},
            {"issue", std::string(issue_to_string(finding.issue))},
        });
    }
    auto response = ok_response();
    response["findings"] = std::move(findings);
    return response;
}

} // namespace

nlohmann::json handle_request(VaultStore& store, const nlohmann::json& request) {
    if (!request.is_object() || !request.contains("verb")) {
        return error_response("malformed request: missing 'verb'");
    }
    const std::string verb = request.value("verb", std::string{});

    if (verb == "status") {
        return handle_status(store);
    }
    if (verb == "create") {
        return handle_create(store, request);
    }
    if (verb == "unlock") {
        return handle_unlock(store, request);
    }
    if (verb == "lock") {
        store.lock();
        return ok_response();
    }
    if (verb == "list") {
        return handle_list(store);
    }
    if (verb == "get") {
        return handle_get(store, request);
    }
    if (verb == "put") {
        return handle_put(store, request);
    }
    if (verb == "delete") {
        return handle_delete(store, request);
    }
    if (verb == "generate_password") {
        return handle_generate_password(request);
    }
    if (verb == "health") {
        return handle_health(store);
    }
    if (verb == "export") {
        return handle_export(store, request);
    }
    return error_response("unknown verb: " + verb);
}

} // namespace nexus::vault
