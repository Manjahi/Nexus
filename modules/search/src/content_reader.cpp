#include "nexus/module/search/content_reader.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <string_view>

namespace nexus::module::search {

namespace {

constexpr std::array<std::string_view, 34> kTextExtensions{
    {".txt",  ".md",   ".markdown", ".log",  ".csv",  ".tsv",  ".json", ".xml",  ".yaml",
     ".yml",  ".toml", ".ini",      ".cfg",  ".conf", ".html", ".htm",  ".rst",  ".tex",
     ".c",    ".h",    ".hpp",      ".hh",   ".cpp",  ".cc",   ".cxx",  ".py",   ".js",
     ".ts",   ".java", ".cs",       ".rs",   ".go",   ".rb",   ".sh"}};

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool looks_binary(std::string_view sample) {
    std::size_t control = 0;
    for (const char ch : sample) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c == 0) {
            return true;
        }
        if (c < 0x09 || (c > 0x0D && c < 0x20)) {
            ++control;
        }
    }
    return !sample.empty() && control * 100 / sample.size() > 5;
}

std::string strip_markup(std::string_view html) {
    std::string out;
    out.reserve(html.size());
    bool in_tag = false;
    for (const char c : html) {
        if (c == '<') {
            in_tag = true;
        } else if (c == '>') {
            in_tag = false;
            out.push_back(' ');
        } else if (!in_tag) {
            out.push_back(c);
        }
    }
    return out;
}

} // namespace

bool is_indexable(const std::filesystem::path& path) {
    const std::string ext = to_lower(path.extension().string());
    return std::find(kTextExtensions.begin(), kTextExtensions.end(), ext) != kTextExtensions.end();
}

std::optional<std::string> read_text(const std::filesystem::path& path, std::size_t max_bytes) {
    if (!is_indexable(path)) {
        return std::nullopt;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }

    std::string data(max_bytes, '\0');
    in.read(data.data(), static_cast<std::streamsize>(max_bytes));
    data.resize(static_cast<std::size_t>(in.gcount()));
    if (in.bad()) {
        return std::nullopt;
    }
    if (looks_binary(std::string_view(data).substr(0, 512))) {
        return std::nullopt;
    }

    const std::string ext = to_lower(path.extension().string());
    if (ext == ".html" || ext == ".htm" || ext == ".xml") {
        return strip_markup(data);
    }
    return data;
}

} // namespace nexus::module::search
