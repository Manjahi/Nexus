# NexusPC

Unified, local-first computer management & protection suite: storage intelligence,
secure vault, network & internet monitoring, system health, backup & recovery, and
local search — one Qt desktop shell over a shared C++ core.

- Product & architecture spec: [`docs/spec/architecture-v1.txt`](docs/spec/architecture-v1.txt)
- Implementation plan: [`docs/IMPLEMENTATION_PLAN.md`](docs/IMPLEMENTATION_PLAN.md)
- Decisions: [`docs/adr/`](docs/adr/)

Status: **Milestone 1 — platform shell (in progress).** Buildable `libnexus-core`
(IDs, time, `Result`), `libnexus-db` (SQLite RAII wrapper, migrations, core
schema, settings repository), `libnexus-jobs` (thread pool, scheduler,
cancellation, progress, retry/backoff), `libnexus-notify` (in-process
notification center), and `app_services` (event bus, audit log, module
registry, service context). Qt shell skeleton and a Catch2 suite (78 tests).
Next: job/notification persistence, then wiring the shell to the services.

## Prerequisites (Windows)

The repo scaffold is complete, but this machine currently has **no C++ build
toolchain**. Install:

| Tool | Install (PowerShell, admin) |
|---|---|
| Visual Studio 2022 Build Tools (MSVC v143, C++ workload, Windows 11 SDK) | `choco install visualstudio2022buildtools --package-parameters "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"` |
| CMake ≥ 3.25 | `choco install cmake --installargs 'ADD_CMAKE_TO_PATH=System'` |
| Ninja | `choco install ninja` |
| Git | already installed |
| vcpkg | `git clone https://github.com/microsoft/vcpkg C:\vcpkg && C:\vcpkg\bootstrap-vcpkg.bat` then set `VCPKG_ROOT=C:\vcpkg` (System env var) |
| Qt 6.6 LTS (Widgets, Charts) — for the desktop app only | Qt Online Installer → `msvc2022_64`, or `aqt install-qt windows desktop 6.6.3 win64_msvc2022_64`. Add its `lib\cmake` to `CMAKE_PREFIX_PATH`. |

Then pin the vcpkg baseline once:

```powershell
cd "path\to\Nexus"
& "$env:VCPKG_ROOT\vcpkg.exe" x-update-baseline --add-initial-baseline
```

## Build

```powershell
# Full build (needs Qt on CMAKE_PREFIX_PATH)
cmake --preset windows-msvc
cmake --build --preset debug
ctest --preset debug

# Core + tests only, no Qt, no vcpkg
cmake --preset windows-no-deps
cmake --build build/windows-no-deps
```

`CMakePresets.json` presets: `windows-msvc` (vcpkg), `windows-no-deps` (core+tests),
`ci-windows` (CI, no desktop app). Pass `-DNEXUSPC_WARNINGS_AS_ERRORS=ON` to make
warnings fatal once the tree is clean under it.

## Layout

```
apps/        desktop (Qt shell), agent, vault          — agent/vault are stubs
libs/        core, db, fs, hash, jobs, net, system, search, crypto, notify
modules/     storage, network_center, connectivity, hardware, backup, search, vault
app_services/ registry, event bus, alerts, reports, permissions, audit
tests/       unit, integration, system
cmake/  docs/  packaging/  .github/
```

Only `libs/core`, `apps/desktop`, and `tests/unit` exist so far; the rest arrive
per the milestone plan.

## Contributing

See [`docs/CODING_STANDARDS.md`](docs/CODING_STANDARDS.md). Formatting is enforced by
`.clang-format`; CI runs `clang-format --Werror` plus the MSVC build and CTest.
