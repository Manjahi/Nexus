#include "nexus/module/search/content_reader.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

#include <zip.h>

namespace fs = std::filesystem;
using namespace nexus::module::search;

namespace {

fs::path make_scratch_path(std::string_view name) {
    const auto tag = std::chrono::steady_clock::now().time_since_epoch().count();
    return fs::temp_directory_path() / ("nexuspc_content_reader_" + std::to_string(tag) + "_" +
                                        std::string(name));
}

void write(const fs::path& p, std::string_view content) {
    std::ofstream out(p, std::ios::binary);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

// Builds a minimal-but-real .docx: a zip archive containing just
// word/document.xml with the given OOXML body. Real Word documents also
// carry [Content_Types].xml, _rels/, etc., but read_docx_text() only ever
// opens word/document.xml by name, so this is a faithful fixture for
// exercising it without needing a real Word installation to produce one.
fs::path make_docx(std::string_view document_xml) {
    const fs::path path = make_scratch_path("fixture.docx");
    int err = 0;
    zip_t* archive = zip_open(path.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    REQUIRE(archive != nullptr);

    zip_source_t* source =
        zip_source_buffer(archive, document_xml.data(), document_xml.size(), 0);
    REQUIRE(source != nullptr);
    const auto index = zip_file_add(archive, "word/document.xml", source, ZIP_FL_OVERWRITE);
    if (index < 0) {
        zip_source_free(source);
    }
    REQUIRE(index >= 0);
    REQUIRE(zip_close(archive) == 0);
    return path;
}

constexpr std::string_view kTwoParagraphs =
    R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
  <w:body>
    <w:p><w:r><w:t>Hello</w:t></w:r></w:p>
    <w:p><w:r><w:t>World</w:t></w:r></w:p>
  </w:body>
</w:document>)";

} // namespace

TEST_CASE("is_indexable recognises .docx alongside the plain-text extensions",
         "[search][content_reader]") {
    REQUIRE(is_indexable("report.docx"));
    REQUIRE(is_indexable("REPORT.DOCX")); // case-insensitive, like every other extension here
    REQUIRE(is_indexable("notes.txt"));
    REQUIRE_FALSE(is_indexable("photo.png"));
    REQUIRE_FALSE(is_indexable("archive.zip")); // a .docx IS a zip, but a bare .zip isn't one
}

TEST_CASE("read_text extracts every <w:t> run's text from a real .docx", "[search][content_reader]") {
    const fs::path path = make_docx(kTwoParagraphs);
    const auto text = read_text(path);
    REQUIRE(text.has_value());
    REQUIRE(text->find("Hello") != std::string::npos);
    REQUIRE(text->find("World") != std::string::npos);

    std::error_code ec;
    fs::remove(path, ec);
}

TEST_CASE("read_text on a .docx with malformed XML returns nullopt, not a crash",
         "[search][content_reader]") {
    const fs::path path = make_docx("this is not valid xml <<<");
    const auto text = read_text(path);
    REQUIRE_FALSE(text.has_value());

    std::error_code ec;
    fs::remove(path, ec);
}

TEST_CASE("read_text on a .docx missing word/document.xml returns nullopt",
         "[search][content_reader]") {
    const fs::path path = make_scratch_path("empty.docx");
    int err = 0;
    zip_t* archive = zip_open(path.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    REQUIRE(archive != nullptr);
    REQUIRE(zip_close(archive) == 0); // valid, but empty, zip archive

    const auto text = read_text(path);
    REQUIRE_FALSE(text.has_value());

    std::error_code ec;
    fs::remove(path, ec);
}

TEST_CASE("read_text on a nonexistent .docx returns nullopt", "[search][content_reader]") {
    const auto text = read_text(make_scratch_path("does_not_exist.docx"));
    REQUIRE_FALSE(text.has_value());
}

TEST_CASE("read_text still handles plain text files", "[search][content_reader]") {
    const fs::path path = make_scratch_path("notes.txt");
    write(path, "plain text content");
    const auto text = read_text(path);
    REQUIRE(text.has_value());
    REQUIRE(*text == "plain text content");

    std::error_code ec;
    fs::remove(path, ec);
}
