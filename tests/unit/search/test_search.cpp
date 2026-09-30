#include "nexus/search/inverted_index.hpp"
#include "nexus/search/snippet.hpp"
#include "nexus/search/tokenizer.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

using nexus::search::InvertedIndex;
using nexus::search::make_snippet;
using nexus::search::tokenize_terms;

TEST_CASE("tokenizer lowercases and splits on non-word characters", "[search][tokenize]") {
    const auto terms = tokenize_terms("Hello, World! nexus_pc build_2024 — a", {});
    REQUIRE(terms == std::vector<std::string>{"hello", "world", "nexus_pc", "build_2024"});
    // trailing "a" is below min_length 2.

    nexus::search::TokenizeOptions keep_short;
    keep_short.min_length = 1;
    REQUIRE(tokenize_terms("v2.0", keep_short) == std::vector<std::string>{"v2", "0"});
}

TEST_CASE("tokenizer can drop stopwords", "[search][tokenize]") {
    nexus::search::TokenizeOptions opts;
    opts.drop_stopwords = true;
    const auto terms = tokenize_terms("the quick brown fox and the lazy dog", opts);
    REQUIRE(terms == std::vector<std::string>{"quick", "brown", "fox", "lazy", "dog"});
}

TEST_CASE("index ranks the more relevant document first", "[search][index]") {
    InvertedIndex index;
    index.add_document(1, "the cat sat on the mat");
    index.add_document(2, "cats and dogs and more cats, a whole document about cats");
    index.add_document(3, "financial report quarterly earnings");

    const auto hits = index.search("cats");
    REQUIRE(hits.size() == 1); // only doc 2 has "cats" (doc 1 has "cat")
    REQUIRE(hits[0].id == 2);

    const auto both = index.search("cat cats");
    REQUIRE(both.size() == 2);
    REQUIRE(both[0].id == 2); // matches "cats"; doc 1 matches "cat"
}

TEST_CASE("more matched query terms outrank a single strong term", "[search][index]") {
    InvertedIndex index;
    index.add_document(1, "alpha alpha alpha alpha alpha");
    index.add_document(2, "alpha beta gamma");

    const auto hits = index.search("alpha beta gamma");
    REQUIRE(hits.size() == 2);
    REQUIRE(hits[0].id == 2); // 3 distinct terms beat 1 repeated term
    REQUIRE(hits[0].matched_terms == 3);
}

TEST_CASE("remove_document updates results and stats", "[search][index]") {
    InvertedIndex index;
    index.add_document(1, "keep this content");
    index.add_document(2, "remove this content");
    REQUIRE(index.document_count() == 2);

    REQUIRE(index.remove_document(2));
    REQUIRE_FALSE(index.remove_document(2));
    REQUIRE(index.document_count() == 1);

    const auto hits = index.search("remove");
    REQUIRE(hits.empty());
    REQUIRE(index.search("content").size() == 1);
}

TEST_CASE("re-adding a document replaces it", "[search][index]") {
    InvertedIndex index;
    index.add_document(1, "first version mentions apples");
    index.add_document(1, "second version mentions oranges");
    REQUIRE(index.document_count() == 1);
    REQUIRE(index.search("apples").empty());
    REQUIRE(index.search("oranges").size() == 1);
}

TEST_CASE("snippet centres on the first match and highlights terms", "[search][snippet]") {
    const std::string text =
        "Introduction paragraph. The important keyword appears here in the middle of a long "
        "body of text that continues well past the snippet window boundary and keeps going.";

    const auto snippet = make_snippet(text, {"keyword"}, 60);
    REQUIRE(snippet.find("[keyword]") != std::string::npos);
    REQUIRE(snippet.size() < text.size());
    REQUIRE(snippet.find("\xE2\x80\xA6") != std::string::npos); // an ellipsis somewhere
}

TEST_CASE("snippet from the start has no leading ellipsis", "[search][snippet]") {
    const std::string text = "keyword at the very beginning of this short text";
    const auto snippet = make_snippet(text, {"keyword"}, 200);
    REQUIRE(snippet == "[keyword] at the very beginning of this short text");
}

TEST_CASE("snippet with no match returns the head of the text", "[search][snippet]") {
    const std::string text = "nothing relevant here at all, just filler words for the test";
    const auto snippet = make_snippet(text, {"absent"}, 20);
    REQUIRE(snippet.rfind("nothing", 0) == 0);
    REQUIRE(snippet.find("\xE2\x80\xA6") != std::string::npos);
}
