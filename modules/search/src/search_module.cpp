#include "nexus/module/search/search_module.hpp"

#include "nexus/db/migration.hpp"
#include "nexus/module/search/search_repository.hpp"

namespace nexus::module::search {

SearchModule::SearchModule() = default;
SearchModule::~SearchModule() = default;

void SearchModule::apply_migrations(nexus::db::Database& db) {
    nexus::db::migrate(db, "search", search_migrations());
}

void SearchModule::start(nexus::services::ServiceContext& /*ctx*/) {}
void SearchModule::stop() {}

} // namespace nexus::module::search
