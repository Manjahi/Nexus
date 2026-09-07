#include "nexus/core/id.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <random>

namespace nexus::core {

namespace {

std::uint64_t random_u64() {
    static thread_local std::mt19937_64 engine{std::random_device{}()};
    return engine();
}

int hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower >= 'a' && lower <= 'f') {
        return 10 + (lower - 'a');
    }
    return -1;
}

} // namespace

Uuid Uuid::generate() {
    Uuid id{};
    const std::uint64_t hi = random_u64();
    const std::uint64_t lo = random_u64();
    for (int i = 0; i < 8; ++i) {
        id.bytes[static_cast<std::size_t>(i)] =
            static_cast<std::uint8_t>((hi >> (8 * (7 - i))) & 0xFF);
        id.bytes[static_cast<std::size_t>(i) + 8] =
            static_cast<std::uint8_t>((lo >> (8 * (7 - i))) & 0xFF);
    }
    // Version 4 + RFC 4122 variant.
    id.bytes[6] = static_cast<std::uint8_t>((id.bytes[6] & 0x0F) | 0x40);
    id.bytes[8] = static_cast<std::uint8_t>((id.bytes[8] & 0x3F) | 0x80);
    return id;
}

std::optional<Uuid> Uuid::parse(std::string_view text) {
    static constexpr std::array<std::size_t, 4> dash_positions{8, 13, 18, 23};
    if (text.size() != 36) {
        return std::nullopt;
    }
    for (const std::size_t pos : dash_positions) {
        if (text[pos] != '-') {
            return std::nullopt;
        }
    }

    Uuid id{};
    std::size_t byte_index = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '-') {
            continue;
        }
        const int high = hex_value(text[i]);
        const int low = (i + 1 < text.size()) ? hex_value(text[i + 1]) : -1;
        if (high < 0 || low < 0) {
            return std::nullopt;
        }
        id.bytes[byte_index++] = static_cast<std::uint8_t>((high << 4) | low);
        ++i;
    }
    return (byte_index == 16) ? std::optional<Uuid>{id} : std::nullopt;
}

std::string Uuid::to_string() const {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(36);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            out.push_back('-');
        }
        out.push_back(digits[(bytes[i] >> 4) & 0x0F]);
        out.push_back(digits[bytes[i] & 0x0F]);
    }
    return out;
}

bool Uuid::is_nil() const noexcept {
    return std::all_of(bytes.begin(), bytes.end(), [](std::uint8_t b) { return b == 0; });
}

} // namespace nexus::core
