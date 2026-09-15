# NexusPC

Unified, local-first computer management & protection suite: storage intelligence,
secure vault, network & internet monitoring, system health, backup & recovery, and
local search — one Qt desktop shell over a shared C++ core.

- **User guide (with screenshots): [`docs/USER_GUIDE.md`](docs/USER_GUIDE.md)**
- Product & architecture spec: [`docs/spec/architecture-v1.txt`](docs/spec/architecture-v1.txt)
- Implementation plan: [`docs/IMPLEMENTATION_PLAN.md`](docs/IMPLEMENTATION_PLAN.md)
- Decisions: [`docs/adr/`](docs/adr/)
- UFR conformance: [`docs/UFR_CONFORMANCE.md`](docs/UFR_CONFORMANCE.md) - Performance pass: [`docs/PERFORMANCE.md`](docs/PERFORMANCE.md)

Status: **Milestones 1-7 done; Milestone 8 (hardening & packaging) in progress.**
Platform: `libnexus-core`,
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
wired to all of it. Milestone 3 is **done**: `libnexus-hash` (BLAKE3 + SHA-256, streaming +
file/prefix), `libnexus-fs` (reusable exclusion rules + a cancellable
recursive walker), the **Storage Intelligence** module (size-group ->
partial-hash -> full-hash duplicate scanner persisting
`file_scans`/`duplicate_groups`/`scanned_files`, a storage-cleanup report,
and safe delete to the Recycle Bin via `IFileOperation` - UFR-013), and the
**Storage** page: pick a folder, scan on the thread pool with a progress bar,
review duplicate groups in a checkbox tree, and move selected copies to the
bin. Milestone 4 is **done**: the **Backup & Recovery** module - a content-addressed
`ObjectStore` (blobs keyed by BLAKE3, so snapshots are incremental by
construction), `BackupEngine` / `RestoreEngine` (full or single-file), snapshot
verify and retention pruning over `backup_jobs` / `snapshots` /
`snapshot_files` / `restore_jobs`; jobs with a schedule ("every 6h") are
re-scheduled on every launch (UFR-014); and the **Backup** page (create a job,
back up now / verify / restore, all on the thread pool with progress).
~205 test cases across 17 ctest suites. Milestone 5 (Local Search) is **done**:
`libnexus-search` (tokenizer, in-memory Okapi BM25 inverted index, query-aware
snippets) plus the **search module** - persisted postings in `search_terms`,
the in-memory index rebuilt from them at startup, text extraction for ~34
source/markup/config extensions (HTML tags stripped, binaries skipped), and a
SEARCH page: query-as-you-type results with snippets, "Index a folder…" on the
thread pool with a progress bar, double-click to open. Milestone 6 (Network
Center) is **done**: the **network_center module** - `NetworkRepository` over
`networks`/`devices`/`checks`/`check_results`, a `NetworkScanner` that
ICMP-pings every host in a user-entered CIDR range (never scanned
automatically), a `DeviceMonitor` background worker that re-pings known
devices on a fixed cadence and notifies on online/offline transitions, and a
network report - plus the **Network** page: add an authorized range, scan for
devices on the thread pool with progress, and a live device table. Milestone 7
(Secure Vault) is **done**: per ADR-0003, `nexuspc-vault` is a separate
isolated process (spec section 2/5, UFR-011) - `libnexus-crypto` (Argon2id +
XChaCha20-Poly1305 + secure memory via libsodium), `libnexus-ipc` (a Windows
named-pipe transport), a standalone encrypted `vault.nxv` format (never the
shared SQLite db), and a `VaultStore`/JSON-IPC protocol serving
status/create/unlock/lock/list/get/put/delete/generate_password/health, with
an auto-lock timer independent of the UI. The desktop **Vault** page talks to
it over `VaultClient`, spawning the process on first visit to the page (never
automatically): create/unlock, browse/add/edit/delete entries, a password
generator, a weak/reused/old health check, and a 30s clipboard-clear timeout
on copy. All of it - IPC transport, crypto, vault file format/store, wire
protocol, and the desktop wiring - is covered by unit tests plus an
end-to-end pass against the real compiled `nexuspc-vault.exe` (raw named
pipe) and the real desktop GUI (simulated clicks/typing through Windows UI
Automation: create vault, add an entry, verify it lists, delete it).
Threat model: `docs/security/vault-threat-model.md`. **Milestone 8
(hardening & packaging) is done**: crash isolation between modules
(UFR-020), a heavy-job conflict guard (UFR-018), per-module retention
settings (UFR-010), job throttling (UFR-017), a full UFR conformance pass
(`docs/UFR_CONFORMANCE.md`), a throughput benchmark tool + profiling pass
that found and fixed a real batching bug in search indexing
(`docs/PERFORMANCE.md`), a Windows installer verified end-to-end - install,
launch, uninstall (`packaging/windows/`), and a user guide with real
screenshots of every page (`docs/USER_GUIDE.md`).

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

### Installer

```powershell
cmake --build --preset release   # Release config; Debug builds aren't for distribution
& "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" packaging\windows\NexusPC.iss
```

See `packaging/windows/README.md` for prerequisites and what the installer does.

## Layout

```
apps/          desktop (Qt shell + Platform), agent (stub), vault (nexuspc-vault process)
libs/          core, db, fs, hash, jobs, net, system, search, crypto, ipc, notify
modules/       storage, network_center, connectivity, hardware, backup, search
app_services/  module registry/host, event bus, audit, heavy-job guard, reports
tools/         bench (throughput profiling CLI)
tests/         unit (integration/system are not yet populated)
docs/          spec, ADRs, security threat model, UFR conformance, performance
packaging/     windows (Inno Setup installer)
cmake/  .github/
```

The vault (`apps/vault`) is deliberately not under `modules/` - it is a
separate OS process (ADR-0003), not a `ServiceContext` module like the
others.

## Contributing

See [`docs/CODING_STANDARDS.md`](docs/CODING_STANDARDS.md). Formatting is enforced by
`.clang-format`; CI runs `clang-format --Werror` plus the MSVC build and CTest.
