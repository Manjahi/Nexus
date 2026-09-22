#pragma once

#include <string>

#include "nexus/services/report_center.hpp"

namespace nexus::module::search {

class SearchRepository;

inline constexpr const char* kSearchReportKind = "search";

[[nodiscard]] std::string render_search_report(SearchRepository& repo,
                                               nexus::services::ReportFormat format);

} // namespace nexus::module::search
