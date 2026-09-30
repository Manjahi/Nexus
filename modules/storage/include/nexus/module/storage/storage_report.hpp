#pragma once

#include "nexus/services/report_center.hpp"

#include <string>

namespace nexus::module::storage {

class StorageRepository;

inline constexpr const char* kStorageCleanupKind = "storage-cleanup";

[[nodiscard]] std::string render_storage_cleanup(StorageRepository& repo,
                                                 nexus::services::ReportFormat format);

} // namespace nexus::module::storage
