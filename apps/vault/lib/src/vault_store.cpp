#include "nexus/vault/vault_store.hpp"

#include "nexus/core/id.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <unordered_map>

namespace nexus::vault {

namespace {

bool is_weak_password(const std::string& password) {
    if (password.size() < 12) {
        return true;
    }
    bool has_lower = false;
    bool has_upper = false;
    bool has_digit = false;
    bool has_symbol = false;
    for (const unsigned char c : password) {
        if (std::islower(c)) {
            has_lower = true;
        } else if (std::isupper(c)) {
            has_upper = true;
        } else if (std::isdigit(c)) {
            has_digit = true;
        } else {
            has_symbol = true;
        }
    }
    const int classes = static_cast<int>(has_lower) + static_cast<int>(has_upper) +
                        static_cast<int>(has_digit) + static_cast<int>(has_symbol);
    return classes < 3;
}

} // namespace

bool VaultStore::create(std::string_view master_password, const nexus::crypto::KdfParams& params) {
    auto result = VaultFile::create(path_, master_password, params);
    if (!result) {
        return false;
    }
    unlocked_ = std::move(*result);
    return true;
}

bool VaultStore::unlock(std::string_view master_password) {
    auto result = VaultFile::unlock(path_, master_password);
    if (!result) {
        return false;
    }
    unlocked_ = std::move(*result);
    return true;
}

void VaultStore::lock() noexcept {
    if (unlocked_) {
        for (Entry& entry : unlocked_->entries) {
            secure_clear(entry);
        }
    }
    unlocked_.reset(); // drops the SecureBuffer key, which zeroes on destruction
}

std::vector<EntrySummary> VaultStore::list() const {
    std::vector<EntrySummary> out;
    if (!unlocked_) {
        return out;
    }
    out.reserve(unlocked_->entries.size());
    for (const Entry& entry : unlocked_->entries) {
        out.push_back(EntrySummary{entry.id, entry.kind, entry.title, entry.username, entry.tags,
                                   entry.updated_at});
    }
    return out;
}

std::optional<Entry> VaultStore::get(std::string_view id) const {
    if (!unlocked_) {
        return std::nullopt;
    }
    const auto it = std::find_if(unlocked_->entries.begin(), unlocked_->entries.end(),
                                 [&](const Entry& e) { return e.id == id; });
    if (it == unlocked_->entries.end()) {
        return std::nullopt;
    }
    return *it;
}

std::optional<std::string> VaultStore::put(Entry entry) {
    if (!unlocked_) {
        return std::nullopt;
    }

    const auto now = nexus::core::now();
    std::string id = entry.id;
    if (id.empty()) {
        id = nexus::core::Uuid::generate().to_string();
        entry.id = id;
        entry.created_at = now;
        entry.updated_at = now;
        unlocked_->entries.push_back(std::move(entry));
    } else {
        const auto it = std::find_if(unlocked_->entries.begin(), unlocked_->entries.end(),
                                     [&](const Entry& e) { return e.id == id; });
        if (it == unlocked_->entries.end()) {
            return std::nullopt;
        }
        entry.created_at = it->created_at;
        entry.updated_at = now;
        *it = std::move(entry);
    }

    if (!persist()) {
        return std::nullopt;
    }
    return id;
}

bool VaultStore::remove(std::string_view id) {
    if (!unlocked_) {
        return false;
    }
    const auto it = std::find_if(unlocked_->entries.begin(), unlocked_->entries.end(),
                                 [&](const Entry& e) { return e.id == id; });
    if (it == unlocked_->entries.end()) {
        return false;
    }
    secure_clear(*it);
    unlocked_->entries.erase(it);
    return persist();
}

std::vector<HealthFinding> VaultStore::health() const {
    std::vector<HealthFinding> findings;
    if (!unlocked_) {
        return findings;
    }

    std::unordered_map<std::string, int> password_counts;
    for (const Entry& entry : unlocked_->entries) {
        if (entry.kind == EntryKind::Password && !entry.password.empty()) {
            ++password_counts[entry.password];
        }
    }

    const auto now = nexus::core::now();
    constexpr auto kOldAge = std::chrono::hours{24 * 365};

    // Weak/Reused/Old are all password-hygiene findings - a secure note has
    // no password to be weak or reused, and "hasn't been touched in a year"
    // isn't a meaningful staleness signal for a note the way it is for a
    // credential, so notes are skipped entirely rather than producing
    // spurious findings.
    for (const Entry& entry : unlocked_->entries) {
        if (entry.kind != EntryKind::Password) {
            continue;
        }
        if (is_weak_password(entry.password)) {
            findings.push_back({entry.id, entry.title, HealthIssue::Weak});
        }
        if (const auto it = password_counts.find(entry.password);
            it != password_counts.end() && it->second > 1) {
            findings.push_back({entry.id, entry.title, HealthIssue::Reused});
        }
        if (now - entry.updated_at > kOldAge) {
            findings.push_back({entry.id, entry.title, HealthIssue::Old});
        }
    }
    return findings;
}

bool VaultStore::persist() {
    if (!unlocked_) {
        return false;
    }
    return VaultFile::save(path_, unlocked_->header, unlocked_->key.span(), unlocked_->entries);
}

bool VaultStore::export_to(const std::filesystem::path& destination) const {
    if (!unlocked_) {
        return false;
    }
    if (std::filesystem::exists(destination)) {
        return false;
    }
    return VaultFile::save(destination, unlocked_->header, unlocked_->key.span(),
                           unlocked_->entries);
}

} // namespace nexus::vault
