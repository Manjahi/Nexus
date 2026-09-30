#pragma once

#include "nexus/services/report_center.hpp"

#include <string>

namespace nexus::module::hardware {

class HardwareRepository;

inline constexpr const char* kSystemDiagnosticKind = "system-diagnostic";

[[nodiscard]] std::string render_system_diagnostic(HardwareRepository& repo,
                                                   nexus::services::ReportFormat format);

} // namespace nexus::module::hardware
