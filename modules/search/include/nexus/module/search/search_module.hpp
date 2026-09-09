#pragma once

#include <string_view>

#include "nexus/services/module.hpp"

namespace nexus::module::search {

/// Local Search module. Indexing and queries run from the UI on the thread
/// pool; the module owns the schema.
class SearchModule : public nexus::services::Module {
public:
    SearchModule();
    ~SearchModule() override;

    [[nodiscard]] std::string_view id() const override { return "search"; }
    void apply_migrations(nexus::db::Database& db) override;
    void start(nexus::services::ServiceContext& ctx) override;
    void stop() override;
};

} // namespace nexus::module::search
