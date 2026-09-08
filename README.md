# NexusPC

Unified, local-first computer management & protection suite: storage intelligence,
secure vault, network & internet monitoring, system health, backup & recovery, and
local search — one Qt desktop shell over a shared C++ core.

- Product & architecture spec: [`docs/spec/architecture-v1.txt`](docs/spec/architecture-v1.txt)
- Implementation plan: [`docs/IMPLEMENTATION_PLAN.md`](docs/IMPLEMENTATION_PLAN.md)
- Decisions: [`docs/adr/`](docs/adr/)

Status: **Milestone 1 complete; Milestone 2 in progress.** Platform: `libnexus-core`,
`libnexus-db`, `libnexus-jobs`, `libnexus-notify`, `app_services` (event bus,
audit, module registry, job + notification persistence, `ServiceContext`), and
the Qt shell wired to all of it (Home / Settings / Alerts, a demo job that
persists across restarts). Milestone 2 is **done**: `libnexus-system` and `libnexus-net` (probes: DNS,
ICMPv4, TCP-connect, HTTP via libcurl, plus loss/latency/jitter stats); a
`Module` / `ModuleHost` framework; per-component schema migrations; the **System
Health** module (samples CPU/mem/disk/processes, edge-triggered threshold
notifications) and **Connectivity Center** module (probes targets, records
samples and outages via a per-target state machine); a `ReportCenter` (UFR-006)
with `system-diagnostic` and `internet-reliability` reports rendered to HTML/CSV
on disk; and the Qt shell's **Performance**, **Internet**, and **Reports** pages
wired to all of it. Milestone 3 in progress: `libnexus-hash` (BLAKE3 + SHA-256, streaming +
file/prefix), `libnexus-fs` (reusable exclusion rules + a cancellable
recursive walker), and the **Storage Intelligence** module — a
size-group -> partial-hash -> full-hash duplicate scanner that persists
`file_scans` / `duplicate_groups` / `scanned_files` and computes reclaimable
space, plus a storage-cleanup report. ~160 test cases across 14 ctest suites.
Next: the Storage page and safe (Recycle Bin) delete.

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
| Qt 6.8.3 (Widgets + Charts) — desktop app only | `pip install aqtinstall pip-system-certs` then `python -m aqt install-qt -b https://download.qt.io windows desktop 6.8.3 win64_msvc2022_64 -m qtcharts -O C:\Qt`. Then set `CMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64`. (`pip-system-certs` makes Python trust the Qt mirror certs; without it `aqt` fails with SSLError. The Qt Online Installer is the GUI alternative.) |

Then pin the vcpkg baseline once:

```powershell
cd "path\to\Nexus"
& "$env:VCPKG_ROOT\vcpkg.exe" x-update-baseline --add-initial-baseline
```

## Build

```powershell
# Full build (needs Qt; run from a VS x64 dev prompt or after vcvars64.bat)
$env:CMAKE_PREFIX_PATH = "C:\Qt\6.8.3\msvc2022_64"
cmake --preset windows-msvc
cmake --build --preset debug
ctest --preset debug

# Backend only (no Qt desktop app)
cmake --preset ci-windows
cmake --build --preset ci
ctest --preset ci
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

So far: `libs/{core,db,jobs,notify}`, `app_services/`, `apps/{desktop,agent,vault}`,
`tests/unit`. The remaining `libs/*` and `modules/*` arrive per the milestone plan.

## Contributing

See [`docs/CODING_STANDARDS.md`](docs/CODING_STANDARDS.md). Formatting is enforced by
`.clang-format`; CI runs `clang-format --Werror` plus the MSVC build and CTest.
