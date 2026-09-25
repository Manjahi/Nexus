#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nexus::module::search {

/// True if the file's extension is one we know how to extract text from.
[[nodiscard]] bool is_indexable(const std::filesystem::path& path);

/// Every extension is_indexable() recognizes (dot included, e.g. ".txt"),
/// for UI enumeration - a Search page filter dropdown, say. is_indexable()
/// itself checks its own static lists directly rather than calling this.
[[nodiscard]] std::vector<std::string_view> known_extensions();

/// Reads up to `max_bytes` of `path` and returns plain text suitable for
/// tokenising. HTML/XML tags are stripped. nullopt for unreadable or
/// unsupported files.
[[nodiscard]] std::optional<std::string> read_text(const std::filesystem::path& path,
                                                   std::size_t max_bytes = 1u << 20);

} // namespace nexus::module::search
