#pragma once

#include <string>

#include "nexus/services/report_center.hpp"

namespace nexus::module::storage {

class StorageRepository;

inline constexpr const char* kStorageCleanupKind = "storage-cleanup";

[[nodiscard]] std::string render_storage_cleanup(StorageRepository& repo,
                                                nexus::services::ReportFormat format);

} // namespace nexus::module::storage
