# Building NexusPC from source

Most people should just run the installer (see the README). This is for
building it yourself.

## Prerequisites (Windows)

| Tool | Install (PowerShell, admin) |
|---|---|
| Visual Studio 2022 Build Tools (MSVC v143, C++ workload, Windows 11 SDK) | `choco install visualstudio2022buildtools --package-parameters "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"` |
| CMake ≥ 3.25 | `choco install cmake --installargs 'ADD_CMAKE_TO_PATH=System'` |
| Ninja | `choco install ninja` |
| Git | usually already installed |
| vcpkg | `git clone https://github.com/microsoft/vcpkg C:\vcpkg && C:\vcpkg\bootstrap-vcpkg.bat` then set `VCPKG_ROOT=C:\vcpkg` (System environment variable) |
| Qt 6.8.3 (Widgets + Charts) — desktop app only | `pip install aqtinstall pip-system-certs` then `python -m aqt install-qt -b https://download.qt.io windows desktop 6.8.3 win64_msvc2022_64 -m qtcharts -O C:\Qt`. Then set `CMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64`. (`pip-system-certs` makes Python trust the Qt mirror certs; without it `aqt` fails with an SSL error. The Qt Online Installer is the GUI alternative.) |
| Inno Setup 6 — only if you want to build the installer | `choco install innosetup -y` |

Then pin the vcpkg baseline once:

```powershell
cd path\to\Nexus
& "$env:VCPKG_ROOT\vcpkg.exe" x-update-baseline --add-initial-baseline
```

## Build

```powershell
# Full build (needs Qt; run from a VS x64 dev prompt, or after vcvars64.bat)
$env:CMAKE_PREFIX_PATH = "C:\Qt\6.8.3\msvc2022_64"
cmake --preset windows-msvc
cmake --build --preset debug        # or: --preset release
ctest --preset debug

# Backend only, no Qt/desktop app - faster, useful for library/module work
cmake --preset ci-windows
cmake --build --preset ci
ctest --preset ci
```

`CMakePresets.json` presets: `windows-msvc` (full build, vcpkg), `windows-no-deps`
(core + tests, no vcpkg), `ci-windows` (backend only, what CI's fast job runs).
Pass `-DNEXUSPC_WARNINGS_AS_ERRORS=ON` to make warnings fatal.

## Building the installer

```powershell
cmake --build --preset release      # Release, not Debug - this is what ships
& "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" packaging\windows\NexusPC.iss
```

Output: `build\installer\NexusPC-Setup-<version>.exe`. Details on what the
installer does and how it was verified: `packaging/windows/README.md`.

## Layout

```
apps/          desktop (Qt shell + Platform), agent (stub), vault (nexuspc-vault process)
libs/          core, db, fs, hash, jobs, net, system, search, crypto, ipc, notify
modules/       storage, network_center, connectivity, hardware, backup, search
app_services/  module registry/host, event bus, audit, heavy-job guard, reports
tools/         bench (throughput profiling CLI)
tests/         unit, integration (system-level coverage lives in desktop_selftest instead)
docs/          spec, ADRs, security threat model, UFR conformance, performance, this file
packaging/     windows (Inno Setup installer)
cmake/  .github/
```

The vault (`apps/vault`) is deliberately not under `modules/` - it's a
separate OS process (see `docs/adr/0003-vault-security-architecture.md`),
not a `ServiceContext` module like the others.

## Tests

- `ctest --preset debug` (or `ci`) runs everything: per-library/per-module
  unit tests, `tests/integration` (real modules against a real database/job
  system), and `desktop_selftest` (a headless full-platform smoke test).
- `docs/CODING_STANDARDS.md` covers formatting (`.clang-format`, enforced in
  CI) and general conventions.
- `docs/PERFORMANCE.md` covers the `tools/bench` throughput benchmark and
  how to run it.
