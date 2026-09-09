#include "nexus/search/snippet.hpp"

#include <algorithm>
#include <cctype>

namespace nexus::search {

namespace {

char lower(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

bool word_boundary(std::string_view text, std::size_t pos, std::size_t len) {
    const bool left = pos == 0 || (std::isalnum(static_cast<unsigned char>(text[pos - 1])) == 0);
    const std::size_t after = pos + len;
    const bool right =
        after >= text.size() || (std::isalnum(static_cast<unsigned char>(text[after])) == 0);
    return left && right;
}

// First whole-word, case-insensitive occurrence of `needle` in `hay` at or after
// `from`, or npos.
std::size_t find_word(std::string_view hay, std::string_view needle, std::size_t from) {
    if (needle.empty()) {
        return std::string_view::npos;
    }
    for (std::size_t i = from; i + needle.size() <= hay.size(); ++i) {
        bool eq = true;
        for (std::size_t j = 0; j < needle.size(); ++j) {
            if (lower(hay[i + j]) != lower(needle[j])) {
                eq = false;
                break;
            }
        }
        if (eq && word_boundary(hay, i, needle.size())) {
            return i;
        }
    }
    return std::string_view::npos;
}

} // namespace

std::string make_snippet(std::string_view text, const std::vector<std::string>& query_terms,
                         std::size_t max_chars, std::string_view mark_open,
                         std::string_view mark_close) {
    // Earliest match across all terms.
    std::size_t first = std::string_view::npos;
    for (const std::string& term : query_terms) {
        const std::size_t at = find_word(text, term, 0);
        if (at != std::string_view::npos) {
            first = std::min(first, at);
        }
    }

    std::size_t begin = 0;
    if (first != std::string_view::npos && first > max_chars / 3) {
        begin = first - max_chars / 3;
        while (begin > 0 && std::isalnum(static_cast<unsigned char>(text[begin - 1])) != 0) {
            --begin; // don't cut mid-word
        }
    }
    std::size_t end = std::min(text.size(), begin + max_chars);
    while (end < text.size() && std::isalnum(static_cast<unsigned char>(text[end])) != 0) {
        ++end;
    }

    std::string_view window = text.substr(begin, end - begin);

    // Highlight matches within the window.
    std::string out;
    if (begin > 0) {
        out += "\xE2\x80\xA6"; // …
    }
    std::size_t cursor = 0;
    while (cursor < window.size()) {
        std::size_t best = std::string_view::npos;
        std::size_t best_len = 0;
        for (const std::string& term : query_terms) {
            const std::size_t at = find_word(window, term, cursor);
            if (at < best) {
                best = at;
                best_len = term.size();
            }
        }
        if (best == std::string_view::npos) {
            out.append(window.substr(cursor));
            break;
        }
        out.append(window.substr(cursor, best - cursor));
        out.append(mark_open);
        out.append(window.substr(best, best_len));
        out.append(mark_close);
        cursor = best + best_len;
    }
    if (end < text.size()) {
        out += "\xE2\x80\xA6";
    }
    return out;
}

} // namespace nexus::search
