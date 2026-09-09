#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace nexus::search {

/// Returns a short excerpt of `text` around the first occurrence of any query
/// term, with leading/trailing ellipses when truncated. Matched terms are
/// wrapped in `mark_open`/`mark_close`. Case-insensitive, whole-word matching.
[[nodiscard]] std::string make_snippet(std::string_view text,
                                       const std::vector<std::string>& query_terms,
                                       std::size_t max_chars = 200,
                                       std::string_view mark_open = "[",
                                       std::string_view mark_close = "]");

} // namespace nexus::search
