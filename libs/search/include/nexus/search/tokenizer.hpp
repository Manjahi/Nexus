#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace nexus::search {

struct Token {
    std::string term;         ///< lowercased
    std::size_t position = 0; ///< 0-based token ordinal
    std::size_t offset = 0;   ///< byte offset of the term in the source text
};

struct TokenizeOptions {
    std::size_t min_length = 2;
    bool drop_stopwords = false;
};

/// Splits text on non-alphanumeric runs, lowercasing ASCII. Underscores and
/// digits are kept (so identifiers and versions survive).
[[nodiscard]] std::vector<Token> tokenize(std::string_view text,
                                          const TokenizeOptions& options = {});

/// Just the terms (for building a query).
[[nodiscard]] std::vector<std::string> tokenize_terms(std::string_view text,
                                                      const TokenizeOptions& options = {});

[[nodiscard]] bool is_stopword(std::string_view term) noexcept;

} // namespace nexus::search
