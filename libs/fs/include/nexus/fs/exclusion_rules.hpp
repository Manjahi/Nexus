#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace nexus::fs {

/// Wildcard match. `*` matches any run of characters (including `/`), `?` matches
/// exactly one. Case-insensitive.
[[nodiscard]] bool glob_match(std::string_view pattern, std::string_view text);

/// Reusable filesystem exclusions shared by scan / index / backup (UFR-008).
///
/// A path is compared with `/` separators, relative to the walk root. A glob
/// with no `/` is also tried against the final path component, so `*.tmp`
/// excludes `a/b/c.tmp`.
class ExclusionRules {
public:
    void exclude_glob(std::string pattern);
    /// Prune any directory whose final component equals `name` (case-insensitive).
    void exclude_directory_name(std::string name);

    [[nodiscard]] bool excludes_path(std::string_view relative_path) const;
    [[nodiscard]] bool prunes_directory(std::string_view directory_name) const;

    [[nodiscard]] bool empty() const noexcept { return globs_.empty() && dir_names_.empty(); }
    [[nodiscard]] const std::vector<std::string>& globs() const noexcept { return globs_; }
    [[nodiscard]] const std::vector<std::string>& directory_names() const noexcept {
        return dir_names_;
    }

    /// One rule per line: bare names become directory-name rules, anything with a
    /// wildcard or `/` becomes a glob. Blank lines and `#` comments are ignored.
    [[nodiscard]] std::string to_text() const;
    [[nodiscard]] static ExclusionRules from_text(std::string_view text);

    /// Common noise: VCS dirs, build output, OS cruft.
    [[nodiscard]] static ExclusionRules defaults();

private:
    std::vector<std::string> globs_;
    std::vector<std::string> dir_names_;
};

} // namespace nexus::fs
