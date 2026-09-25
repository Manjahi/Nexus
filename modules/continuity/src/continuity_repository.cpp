#include "nexus/module/continuity/continuity_repository.hpp"

#include "nexus/db/database.hpp"
#include "nexus/db/statement.hpp"

namespace nexus::module::continuity {

std::string_view to_string(AssetKind kind) noexcept {
    switch (kind) {
        case AssetKind::File: return "file";
        case AssetKind::Folder: return "folder";
        case AssetKind::App: return "app";
        case AssetKind::Credential: return "credential";
    }
    return "file";
}

std::optional<AssetKind> asset_kind_from_string(std::string_view text) noexcept {
    if (text == "file") return AssetKind::File;
    if (text == "folder") return AssetKind::Folder;
    if (text == "app") return AssetKind::App;
    if (text == "credential") return AssetKind::Credential;
    return std::nullopt;
}

std::string_view to_string(Criticality criticality) noexcept {
    switch (criticality) {
        case Criticality::Low: return "low";
        case Criticality::Medium: return "medium";
        case Criticality::High: return "high";
    }
    return "medium";
}

std::optional<Criticality> criticality_from_string(std::string_view text) noexcept {
    if (text == "low") return Criticality::Low;
    if (text == "medium") return Criticality::Medium;
    if (text == "high") return Criticality::High;
    return std::nullopt;
}

namespace {

constexpr const char* kAssetColumns =
    "id, label, kind, path, vault_entry_id, criticality, created_at";

TrackedAsset read_asset(nexus::db::Statement& stmt) {
    TrackedAsset asset;
    if (const auto id = nexus::core::Uuid::parse(stmt.column_text(0))) {
        asset.id = *id;
    }
    asset.label = stmt.column_text(1);
    asset.kind = asset_kind_from_string(stmt.column_text(2)).value_or(AssetKind::File);
    asset.path = stmt.column_text(3);
    asset.vault_entry_id = stmt.column_text(4);
    asset.criticality =
        criticality_from_string(stmt.column_text(5)).value_or(Criticality::Medium);
    if (const auto at = nexus::core::from_iso8601(stmt.column_text(6))) {
        asset.created_at = *at;
    }
    return asset;
}

constexpr const char* kRehearsalColumns =
    "id, scenario, started_at, finished_at, outcome, files_restored, bytes_restored, detail";

RehearsalRecord read_rehearsal(nexus::db::Statement& stmt) {
    RehearsalRecord rec;
    if (const auto id = nexus::core::Uuid::parse(stmt.column_text(0))) {
        rec.id = *id;
    }
    rec.scenario = stmt.column_text(1);
    if (const auto at = nexus::core::from_iso8601(stmt.column_text(2))) {
        rec.started_at = *at;
    }
    if (!stmt.column_is_null(3)) {
        rec.finished_at = nexus::core::from_iso8601(stmt.column_text(3));
    }
    rec.outcome = stmt.column_text(4);
    rec.files_restored = static_cast<std::uint64_t>(stmt.column_int64(5));
    rec.bytes_restored = static_cast<std::uint64_t>(stmt.column_int64(6));
    rec.detail = stmt.column_text(7);
    return rec;
}

} // namespace

nexus::core::Uuid ContinuityRepository::upsert_asset(const TrackedAsset& asset) {
    const nexus::core::Uuid id = asset.id.is_nil() ? nexus::core::Uuid::generate() : asset.id;
    const std::string now = nexus::core::to_iso8601(nexus::core::now());
    const std::string created = asset.created_at.time_since_epoch().count() == 0
                                    ? now
                                    : nexus::core::to_iso8601(asset.created_at);

    nexus::db::Statement stmt = db_->prepare(
        "INSERT INTO continuity_assets (id, label, kind, path, vault_entry_id, criticality, "
        "created_at) VALUES (?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(id) DO UPDATE SET label = excluded.label, kind = excluded.kind, "
        "path = excluded.path, vault_entry_id = excluded.vault_entry_id, "
        "criticality = excluded.criticality");
    stmt.bind(1, id.to_string());
    stmt.bind(2, asset.label);
    stmt.bind(3, std::string(to_string(asset.kind)));
    stmt.bind(4, asset.path);
    stmt.bind(5, asset.vault_entry_id);
    stmt.bind(6, std::string(to_string(asset.criticality)));
    stmt.bind(7, created);
    stmt.step();
    return id;
}

std::optional<TrackedAsset> ContinuityRepository::find_asset(const nexus::core::Uuid& id) const {
    nexus::db::Statement stmt = db_->prepare(
        std::string("SELECT ") + kAssetColumns + " FROM continuity_assets WHERE id = ?");
    stmt.bind(1, id.to_string());
    if (!stmt.step()) {
        return std::nullopt;
    }
    return read_asset(stmt);
}

std::vector<TrackedAsset> ContinuityRepository::list_assets() const {
    nexus::db::Statement stmt = db_->prepare(std::string("SELECT ") + kAssetColumns +
                                             " FROM continuity_assets ORDER BY created_at ASC");
    std::vector<TrackedAsset> out;
    while (stmt.step()) {
        out.push_back(read_asset(stmt));
    }
    return out;
}

bool ContinuityRepository::remove_asset(const nexus::core::Uuid& id) {
    nexus::db::Statement stmt = db_->prepare("DELETE FROM continuity_assets WHERE id = ?");
    stmt.bind(1, id.to_string());
    stmt.step();
    return stmt.changes() > 0;
}

nexus::core::Uuid ContinuityRepository::begin_rehearsal(std::string_view scenario) {
    const nexus::core::Uuid id = nexus::core::Uuid::generate();
    nexus::db::Statement stmt = db_->prepare(
        "INSERT INTO continuity_rehearsals (id, scenario, started_at, outcome) "
        "VALUES (?, ?, ?, 'running')");
    stmt.bind(1, id.to_string());
    stmt.bind(2, std::string(scenario));
    stmt.bind(3, nexus::core::to_iso8601(nexus::core::now()));
    stmt.step();
    return id;
}

void ContinuityRepository::finish_rehearsal(const nexus::core::Uuid& id, std::string_view outcome,
                                            std::uint64_t files_restored,
                                            std::uint64_t bytes_restored,
                                            std::string_view detail) {
    nexus::db::Statement stmt = db_->prepare(
        "UPDATE continuity_rehearsals SET finished_at = ?, outcome = ?, files_restored = ?, "
        "bytes_restored = ?, detail = ? WHERE id = ?");
    stmt.bind(1, nexus::core::to_iso8601(nexus::core::now()));
    stmt.bind(2, std::string(outcome));
    stmt.bind(3, static_cast<std::int64_t>(files_restored));
    stmt.bind(4, static_cast<std::int64_t>(bytes_restored));
    stmt.bind(5, std::string(detail));
    stmt.bind(6, id.to_string());
    stmt.step();
}

std::vector<RehearsalRecord> ContinuityRepository::recent_rehearsals(std::size_t limit) const {
    nexus::db::Statement stmt =
        db_->prepare(std::string("SELECT ") + kRehearsalColumns +
                     " FROM continuity_rehearsals ORDER BY started_at DESC, rowid DESC LIMIT ?");
    stmt.bind(1, static_cast<std::int64_t>(limit));
    std::vector<RehearsalRecord> out;
    while (stmt.step()) {
        out.push_back(read_rehearsal(stmt));
    }
    return out;
}

std::optional<RehearsalRecord> ContinuityRepository::latest_rehearsal() const {
    const auto recent = recent_rehearsals(1);
    if (recent.empty()) {
        return std::nullopt;
    }
    return recent.front();
}

void ContinuityRepository::record_capsule_export(nexus::core::Timestamp at) {
    nexus::db::Statement stmt = db_->prepare(
        "INSERT INTO continuity_capsule (id, exported_at) VALUES (1, ?) "
        "ON CONFLICT(id) DO UPDATE SET exported_at = excluded.exported_at");
    stmt.bind(1, nexus::core::to_iso8601(at));
    stmt.step();
}

std::optional<nexus::core::Timestamp> ContinuityRepository::latest_capsule_export() const {
    nexus::db::Statement stmt =
        db_->prepare("SELECT exported_at FROM continuity_capsule WHERE id = 1");
    if (!stmt.step()) {
        return std::nullopt;
    }
    return nexus::core::from_iso8601(stmt.column_text(0));
}

} // namespace nexus::module::continuity
