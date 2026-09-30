#include "nexus/fs/exclusion_rules.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

namespace nexus::fs {

namespace {

char lower(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

bool iequals(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
                                              [](char x, char y) { return lower(x) == lower(y); });
}

std::string_view basename(std::string_view path) {
    const auto slash = path.find_last_of('/');
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

std::string trim(std::string_view s) {
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) {
        return {};
    }
    const auto end = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(begin, end - begin + 1));
}

} // namespace

bool glob_match(std::string_view pattern, std::string_view text) {
    std::size_t p = 0;
    std::size_t t = 0;
    std::size_t star = std::string_view::npos;
    std::size_t star_match = 0;

    while (t < text.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || lower(pattern[p]) == lower(text[t]))) {
            ++p;
            ++t;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            star_match = t;
        } else if (star != std::string_view::npos) {
            p = star + 1;
            t = ++star_match;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') {
        ++p;
    }
    return p == pattern.size();
}

void ExclusionRules::exclude_glob(std::string pattern) {
    if (!pattern.empty()) {
        globs_.push_back(std::move(pattern));
    }
}

void ExclusionRules::exclude_directory_name(std::string name) {
    if (!name.empty()) {
        dir_names_.push_back(std::move(name));
    }
}

bool ExclusionRules::excludes_path(std::string_view relative_path) const {
    const std::string_view leaf = basename(relative_path);
    for (const std::string& glob : globs_) {
        if (glob_match(glob, relative_path)) {
            return true;
        }
        if (glob.find('/') == std::string::npos && glob_match(glob, leaf)) {
            return true;
        }
    }
    for (const std::string& name : dir_names_) {
        if (iequals(name, leaf)) {
            return true;
        }
    }
    return false;
}

bool ExclusionRules::prunes_directory(std::string_view directory_name) const {
    for (const std::string& name : dir_names_) {
        if (iequals(name, directory_name)) {
            return true;
        }
    }
    for (const std::string& glob : globs_) {
        if (glob.find('/') == std::string::npos && glob_match(glob, directory_name)) {
            return true;
        }
    }
    return false;
}

std::string ExclusionRules::to_text() const {
    std::string out;
    for (const std::string& name : dir_names_) {
        out += name;
        out += '\n';
    }
    for (const std::string& glob : globs_) {
        out += glob;
        out += '\n';
    }
    return out;
}

ExclusionRules ExclusionRules::from_text(std::string_view text) {
    ExclusionRules rules;
    std::istringstream stream{std::string(text)};
    std::string line;
    while (std::getline(stream, line)) {
        const std::string rule = trim(line);
        if (rule.empty() || rule.front() == '#') {
            continue;
        }
        if (rule.find_first_of("*?/") == std::string::npos) {
            rules.exclude_directory_name(rule);
        } else {
            rules.exclude_glob(rule);
        }
    }
    return rules;
}

ExclusionRules ExclusionRules::defaults() {
    ExclusionRules rules;
    for (const char* dir : {".git", ".svn", ".hg", "node_modules", "__pycache__", ".venv", "build",
                            "out", ".vs", ".idea"}) {
        rules.exclude_directory_name(dir);
    }
    for (const char* glob :
         {"*.tmp", "*.temp", "*.pyc", "*.o", "*.obj", "Thumbs.db", "desktop.ini", ".DS_Store"}) {
        rules.exclude_glob(glob);
    }
    return rules;
}

} // namespace nexus::fs
