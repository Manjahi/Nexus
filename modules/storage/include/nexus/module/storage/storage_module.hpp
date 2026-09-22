#pragma once

#include <string>
#include <string_view>

#include "nexus/jobs/schedule_table.hpp"
#include "nexus/services/module.hpp"
#include "nexus/services/report_center.hpp"

namespace nexus::module::storage {

/// Storage Intelligence module. Scans are user-initiated (run from the UI on the
/// thread pool), so the module only owns the schema, the cleanup report, and a
/// periodic prune of old scan history (UFR-010: retention.storage.keep_scans).
class StorageModule : public nexus::services::Module {
public:
    StorageModule();
    ~StorageModule() override;

    [[nodiscard]] std::string_view id() const override { return "storage"; }
    void apply_migrations(nexus::db::Database& db) override;
    void start(nexus::services::ServiceContext& ctx) override;
    void stop() override;

    /// Spec section 9 hook #6 (Search->Storage "is this a duplicate?"): is
    /// `absolute_path` part of any duplicate group in the *latest* scan?
    /// Unlike BackupModule::is_path_backed_up(), no source-root bridging is
    /// needed - DuplicateScanner and SearchIndexer both store absolute,
    /// generic_string()-form paths, so this is a direct match. False if the
    /// module hasn't started yet or there's no scan / no match.
    [[nodiscard]] bool is_duplicate_file(const std::string& absolute_path) const;

private:
    nexus::services::ServiceContext* ctx_ = nullptr;
    nexus::jobs::ScheduleTable::Id schedule_id_{};
    nexus::services::ReportCenter::GeneratorId report_id_{};
    bool scheduled_ = false;
    bool report_registered_ = false;
};

} // namespace nexus::module::storage
