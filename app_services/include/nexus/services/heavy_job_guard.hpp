#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace nexus::services {

/// Tracks which resource-intensive jobs (storage scan, backup run/verify/
/// restore, search indexing, network discovery) are currently running, so a
/// call site can warn the user before piling another one on top (UFR-018:
/// "avoid running conflicting heavy jobs simultaneously unless the user
/// allows it").
///
/// This class only tracks holders - it never refuses an acquire(). The
/// "unless the user allows it" policy belongs at the call site (usually the
/// UI, which is what can show a confirmation dialog): check active() first,
/// ask the user if it's non-empty, and only call acquire() once they've
/// decided to proceed (or there was nothing to ask about). Thread-safe.
class HeavyJobGuard {
public:
    /// RAII handle: releases its slot on destruction, including on an
    /// exception unwind. Move-only; a default-constructed Lease holds
    /// nothing and releases nothing.
    class Lease {
    public:
        Lease() noexcept = default;
        ~Lease();

        Lease(Lease&& other) noexcept;
        Lease& operator=(Lease&& other) noexcept;
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;

    private:
        friend class HeavyJobGuard;
        Lease(HeavyJobGuard& guard, std::uint64_t token) noexcept : guard_(&guard), token_(token) {}

        HeavyJobGuard* guard_ = nullptr;
        std::uint64_t token_ = 0;
    };

    /// Labels of every heavy job currently holding a lease (e.g. "Storage
    /// scan", "Backup: Nightly"), in acquisition order. Empty if none.
    [[nodiscard]] std::vector<std::string> active() const;

    /// Registers `label` as running and returns a Lease that unregisters it
    /// when destroyed. Always succeeds.
    [[nodiscard]] Lease acquire(std::string label);

private:
    void release(std::uint64_t token);

    mutable std::mutex mutex_;
    std::uint64_t next_token_ = 1;
    std::vector<std::pair<std::uint64_t, std::string>> holders_;
};

} // namespace nexus::services
