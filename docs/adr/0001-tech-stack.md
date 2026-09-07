# ADR-0001: Technology stack

- Status: Proposed
- Date: 2026-09-07
- Context: NexusPC spec v1.0, greenfield, Windows-first, portfolio project.

## Decision

| Concern | Decision | Rejected alternatives / notes |
|---|---|---|
| Language | C++20, MSVC (VS 2022); C only for low-level OS integration | C++23 not fully available on target toolchain yet; use `std::expected` polyfill |
| Build system | CMake >= 3.25 with `CMakePresets.json`, Ninja generator | Premake, MSBuild-only |
| Dependency manager | vcpkg (manifest mode, pinned baseline) | Conan 2 (viable; vcpkg has smoother Qt/MSVC path); system packages |
| GUI toolkit | Qt 6.6 LTS, Widgets + Qt Charts | Qt QML (heavier for dense tables/dashboards; may add later), Dear ImGui, wxWidgets |
| Qt install method | TBD in Phase 0 — spike vcpkg build vs official online installer | Decision to be appended here |
| Local database | SQLite 3, hand-rolled RAII wrapper + migration runner | SQLiteCpp / sqlite_orm (less portfolio value); PostgreSQL (overkill for local-first) |
| Vault crypto | libsodium — Argon2id KDF, XChaCha20-Poly1305 AEAD, secure memory | OpenSSL (heavier API), Windows DPAPI alone (insufficient) |
| Hashing | BLAKE3 for scan/dedup/backup; SHA-256 for verification/interop | xxHash (not cryptographic), MD5 (broken) |
| Networking probes | Win32 `IcmpSendEcho2`, raw TCP connect, libcurl for HTTP/DNS/speed tests | Qt Network (less control over raw probes) |
| System metrics (Windows) | PDH, `GetSystemTimes`, `GlobalMemoryStatusEx`, IPHLPAPI, Toolhelp32, WMI, `GetSystemPowerStatus` | Third-party sensor libs deferred; temps are best-effort |
| Filesystem | `std::filesystem` + `ReadDirectoryChangesW` watcher; `\\?\` long paths | Boost.Filesystem |
| Job scheduling | Custom thread pool + scheduler, `std::stop_token` cancellation, SQLite-persisted schedules | Boost.Asio thread pool, third-party cron libs |
| Logging | spdlog, wrapped behind a `libnexus-core` interface | glog, hand-rolled |
| Serialization / settings | nlohmann/json for settings files; SQLite for structured metadata | TOML, XML |
| Error handling | `Result<T,E>` / `std::expected` in core; exceptions only at API boundaries | exceptions everywhere, error codes everywhere |
| Testing | Catch2 v3 + CTest; nanobench for microbenchmarks | GoogleTest (also fine), doctest |
| Static analysis | clang-format, clang-tidy, cppcheck; ASan/UBSan in a supplementary Linux CI job | |
| CI | GitHub Actions, Windows runner: configure -> build -> ctest | Azure Pipelines |
| Packaging | `windeployqt` + Inno Setup (WiX as alternative) | MSIX (later), portable zip only |

## Consequences

- Windows-first: OS-specific code is isolated behind `libnexus-system` / `libnexus-net` / `libnexus-fs` provider interfaces so Linux/macOS providers can be added without touching modules.
- vcpkg manifest pins reproducible dependency versions; Qt build time is the main risk (see Phase 0 spike).
- Hand-rolling the SQLite wrapper and (later) the search index is a deliberate portfolio choice, accepting more code to own.
