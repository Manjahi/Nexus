#pragma once

#include <type_traits>
#include <utility>
#include <variant>

namespace nexus::core {

/// Minimal success-or-error value.
///
/// Bootstrap stand-in for std::expected (C++23). Intentionally small; extend
/// (monadic ops, void specialisation) as call sites demand.
template <class T, class E>
class Result {
    static_assert(!std::is_same_v<T, E>, "Result<T, E> requires distinct T and E");
    static_assert(!std::is_reference_v<T> && !std::is_reference_v<E>,
                  "Result stores values, not references");

public:
    static Result ok(T value) { return Result(std::in_place_index<0>, std::move(value)); }
    static Result err(E error) { return Result(std::in_place_index<1>, std::move(error)); }

    [[nodiscard]] bool has_value() const noexcept { return data_.index() == 0; }
    explicit operator bool() const noexcept { return has_value(); }

    [[nodiscard]] T& value() & { return std::get<0>(data_); }
    [[nodiscard]] const T& value() const& { return std::get<0>(data_); }
    [[nodiscard]] T&& value() && { return std::get<0>(std::move(data_)); }

    [[nodiscard]] E& error() & { return std::get<1>(data_); }
    [[nodiscard]] const E& error() const& { return std::get<1>(data_); }
    [[nodiscard]] E&& error() && { return std::get<1>(std::move(data_)); }

    template <class U>
    [[nodiscard]] T value_or(U&& fallback) const& {
        return has_value() ? value() : static_cast<T>(std::forward<U>(fallback));
    }

    friend bool operator==(const Result&, const Result&) = default;

private:
    template <std::size_t I, class V>
    Result(std::in_place_index_t<I> tag, V&& v) : data_(tag, std::forward<V>(v)) {}

    std::variant<T, E> data_;
};

} // namespace nexus::core
