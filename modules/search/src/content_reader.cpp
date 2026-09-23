#include "nexus/module/search/content_reader.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <string_view>

#include <pugixml.hpp>
#include <zip.h>

namespace nexus::module::search {

namespace {

constexpr std::array<std::string_view, 34> kTextExtensions{
    {".txt",  ".md",   ".markdown", ".log",  ".csv",  ".tsv",  ".json", ".xml",  ".yaml",
     ".yml",  ".toml", ".ini",      ".cfg",  ".conf", ".html", ".htm",  ".rst",  ".tex",
     ".c",    ".h",    ".hpp",      ".hh",   ".cpp",  ".cc",   ".cxx",  ".py",   ".js",
     ".ts",   ".java", ".cs",       ".rs",   ".go",   ".rb",   ".sh"}};

// Extensions read via a dedicated structured-document extractor rather than
// as raw text - see read_docx_text().
constexpr std::array<std::string_view, 1> kDocumentExtensions{{".docx"}};

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

// A .docx is a zip archive; the document body lives in word/document.xml as
// OOXML, with each run's text in a <w:t> element. pugixml doesn't resolve
// namespace URIs by default - it treats "w:t" as a literal element name -
// which is exactly what's wanted here: a plain string match on the prefix
// Word itself always uses, with no need to register/resolve the real
// namespace. Paragraph/run structure is discarded; every run's text is
// joined with a space, which is enough for tokenising and snippets even
// though it loses exact formatting.
std::optional<std::string> read_docx_text(const std::filesystem::path& path,
                                          std::size_t max_bytes) {
    int err = 0;
    zip_t* archive = zip_open(path.string().c_str(), ZIP_RDONLY, &err);
    if (archive == nullptr) {
        return std::nullopt;
    }

    zip_stat_t stat;
    zip_stat_init(&stat);
    if (zip_stat(archive, "word/document.xml", 0, &stat) != 0) {
        zip_close(archive);
        return std::nullopt;
    }

    zip_file_t* file = zip_fopen(archive, "word/document.xml", 0);
    if (file == nullptr) {
        zip_close(archive);
        return std::nullopt;
    }

    const auto size = std::min(stat.size, static_cast<zip_uint64_t>(max_bytes));
    std::string xml(static_cast<std::size_t>(size), '\0');
    const zip_int64_t read = zip_fread(file, xml.data(), size);
    zip_fclose(file);
    zip_close(archive);
    if (read < 0) {
        return std::nullopt;
    }
    xml.resize(static_cast<std::size_t>(read));

    pugi::xml_document doc;
    if (!doc.load_buffer(xml.data(), xml.size())) {
        return std::nullopt;
    }

    std::string out;
    for (const auto& match : doc.select_nodes("//w:t")) {
        out += match.node().text().get();
        out += ' ';
    }
    return out;
}

} // namespace

bool is_indexable(const std::filesystem::path& path) {
    const std::string ext = to_lower(path.extension().string());
    return std::find(kTextExtensions.begin(), kTextExtensions.end(), ext) != kTextExtensions.end() ||
          std::find(kDocumentExtensions.begin(), kDocumentExtensions.end(), ext) !=
              kDocumentExtensions.end();
}

std::optional<std::string> read_text(const std::filesystem::path& path, std::size_t max_bytes) {
    if (!is_indexable(path)) {
        return std::nullopt;
    }

    const std::string ext = to_lower(path.extension().string());
    if (ext == ".docx") {
        return read_docx_text(path, max_bytes);
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

    if (ext == ".html" || ext == ".htm" || ext == ".xml") {
        return strip_markup(data);
    }
    return data;
}

} // namespace nexus::module::search
