#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace nexus::hash {

enum class Algorithm {
    Blake3,
    Sha256,
};

[[nodiscard]] std::string_view to_string(Algorithm algorithm) noexcept;

/// Both supported digests are 32 bytes.
using Digest = std::array<std::uint8_t, 32>;

[[nodiscard]] std::string to_hex(const Digest& digest);
[[nodiscard]] std::optional<Digest> digest_from_hex(std::string_view text);

/// Streaming hasher. Not thread-safe.
class Hasher {
public:
    explicit Hasher(Algorithm algorithm = Algorithm::Blake3);
    ~Hasher();

    Hasher(Hasher&&) noexcept;
    Hasher& operator=(Hasher&&) noexcept;
    Hasher(const Hasher&) = delete;
    Hasher& operator=(const Hasher&) = delete;

    void update(std::span<const std::byte> data);
    [[nodiscard]] Digest finish();

    [[nodiscard]] Algorithm algorithm() const noexcept { return algorithm_; }

private:
    struct State;
    Algorithm algorithm_;
    std::unique_ptr<State> state_;
};

[[nodiscard]] Digest hash_bytes(std::span<const std::byte> data,
                                Algorithm algorithm = Algorithm::Blake3);

/// Hashes a whole file with buffered reads. nullopt on an IO error.
[[nodiscard]] std::optional<Digest> hash_file(const std::filesystem::path& path,
                                              Algorithm algorithm = Algorithm::Blake3);

/// Hashes at most the first `max_bytes` of a file (the dedup "partial hash"
/// stage). nullopt on an IO error.
[[nodiscard]] std::optional<Digest> hash_file_prefix(const std::filesystem::path& path,
                                                     std::uint64_t max_bytes,
                                                     Algorithm algorithm = Algorithm::Blake3);

} // namespace nexus::hash
