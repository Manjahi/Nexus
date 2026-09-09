#include "nexus/search/tokenizer.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace nexus::search {

namespace {

bool is_word_char(unsigned char c) {
    return std::isalnum(c) != 0 || c == '_';
}

// A small English stopword set - enough to trim the most common noise.
constexpr std::array<std::string_view, 30> kStopwords{
    {"the",  "a",    "an",  "and", "or",   "but",  "if",   "of",   "to",   "in",
     "on",   "for",  "is",  "are", "was",  "were", "be",   "been", "it",   "its",
     "this", "that", "with", "as", "at",   "by",   "from", "not",  "no",   "you"}};

} // namespace

bool is_stopword(std::string_view term) noexcept {
    return std::find(kStopwords.begin(), kStopwords.end(), term) != kStopwords.end();
}

std::vector<Token> tokenize(std::string_view text, const TokenizeOptions& options) {
    std::vector<Token> tokens;
    std::string current;
    std::size_t start = 0;
    std::size_t ordinal = 0;

    auto flush = [&](std::size_t end_offset) {
        if (current.size() >= options.min_length &&
            !(options.drop_stopwords && is_stopword(current))) {
            tokens.push_back({current, ordinal++, start});
        }
        current.clear();
        (void)end_offset;
    };

    for (std::size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (is_word_char(c)) {
            if (current.empty()) {
                start = i;
            }
            current.push_back(static_cast<char>(std::tolower(c)));
        } else if (!current.empty()) {
            flush(i);
        }
    }
    if (!current.empty()) {
        flush(text.size());
    }
    return tokens;
}

std::vector<std::string> tokenize_terms(std::string_view text, const TokenizeOptions& options) {
    std::vector<std::string> terms;
    for (auto& token : tokenize(text, options)) {
        terms.push_back(std::move(token.term));
    }
    return terms;
}

} // namespace nexus::search
