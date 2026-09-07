#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace nexus::core {

/// 128-bit identifier rendered in canonical 8-4-4-4-12 lowercase hex form.
///
/// generate() currently produces a random (version 4) UUID. TODO: switch to a
/// time-ordered UUIDv7 once the suite needs lexicographically sortable ids.
struct Uuid {
    std::array<std::uint8_t, 16> bytes{};

    [[nodiscard]] static Uuid generate();
    [[nodiscard]] static std::optional<Uuid> parse(std::string_view text);

    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] bool is_nil() const noexcept;

    friend bool operator==(const Uuid&, const Uuid&) = default;
    friend auto operator<=>(const Uuid&, const Uuid&) = default;
};

} // namespace nexus::core
