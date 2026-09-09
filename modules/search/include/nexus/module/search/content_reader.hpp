#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

namespace nexus::module::search {

/// True if the file's extension is one we know how to extract text from.
[[nodiscard]] bool is_indexable(const std::filesystem::path& path);

/// Reads up to `max_bytes` of `path` and returns plain text suitable for
/// tokenising. HTML/XML tags are stripped. nullopt for unreadable or
/// unsupported files.
[[nodiscard]] std::optional<std::string> read_text(const std::filesystem::path& path,
                                                   std::size_t max_bytes = 1u << 20);

} // namespace nexus::module::search
