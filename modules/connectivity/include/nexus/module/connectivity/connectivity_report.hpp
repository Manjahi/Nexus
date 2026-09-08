#pragma once

#include <string>

#include "nexus/services/report_center.hpp"

namespace nexus::module::connectivity {

class ConnectivityRepository;

inline constexpr const char* kInternetReliabilityKind = "internet-reliability";

[[nodiscard]] std::string render_internet_reliability(ConnectivityRepository& repo,
                                                     nexus::services::ReportFormat format);

} // namespace nexus::module::connectivity
