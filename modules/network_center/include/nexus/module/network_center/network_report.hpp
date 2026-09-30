#pragma once

#include "nexus/services/report_center.hpp"

#include <string>

namespace nexus::module::network_center {

class NetworkRepository;

inline constexpr const char* kNetworkKind = "network";

[[nodiscard]] std::string render_network_report(NetworkRepository& repo,
                                                nexus::services::ReportFormat format);

} // namespace nexus::module::network_center
