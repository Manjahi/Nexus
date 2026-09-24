# NexusPC — Scoped Implementation Plan

Derived from `NexusPC_Unified_Computer_Management_Suite_Architecture.txt` (spec v1.0).

**Locked decisions**
- Platform scope: **Windows-first** (Windows 10/11). OS-adapter interfaces designed clean; Linux/macOS providers deferred.
- Process model: **In-process modules first** — single `nexuspc-ui` process, modules as libraries. `nexuspc-agent` and `nexuspc-vault` split out in later milestones.
- See `docs/adr/0001-tech-stack.md` and `docs/adr/0002-process-model-and-platform-scope.md`.

**Current state (as originally written, 2026-09-07):** greenfield — no
code, no build system, not a git repo. This document is the original
scoped plan and is kept as-written for historical reference (decisions,
sizing, risks) - it is not a living status doc and has not been updated
to track progress since. **For current implementation status, see
`docs/UFR_CONFORMANCE.md`**, which is actively maintained: all 8
milestones below are now complete, plus a further 9-phase gap-closure
pass (2026-09-22/23) that closed real gaps a code audit found beyond
what UFR_CONFORMANCE.md's own self-reported status had claimed - see
that document's "gap-closure plan" section for what those phases covered
(cross-module EventBus hooks / spec section 9, DNS/speed-test probes,
Network Center ARP, Backup UNC targets, Search incremental indexing and
.docx parsing, Secure Vault notes/export/real-process-IPC test, and
more). PDF parsing (mentioned in Phase 5 below as a stretch item) was
deliberately deferred rather than attempted, per the gap-closure plan's
own explicit call not to let it block everything after it.

---

## 1. Tech stack

| Area | Choice | Notes |
|---|---|---|
| Language | C++20 (MSVC / VS 2022), C for OS glue | Spec section 15 |
| Build | CMake >= 3.25 + `CMakePresets.json` + Ninja | |
| Dependencies | vcpkg (manifest mode) | Conan 2 is the alternative |
| UI | Qt 6.6 LTS — Widgets (+ Qt Charts) | Widgets over QML for dense dashboards; QML optional later |
| Database | SQLite 3 + hand-rolled RAII wrapper + migration runner | `SQLiteCpp` is the shortcut |
| Crypto (Vault) | libsodium — Argon2id KDF, XChaCha20-Poly1305, secure memory | Spec section 6 |
| Hashing | BLAKE3 (scan/dedup/backup) + SHA-256 (verification) | |
| Networking | Win32 `IcmpSendEcho2`, raw TCP connect probes, libcurl for HTTP/DNS/speed | |
| System metrics | PDH counters, `GetSystemTimes`, `GlobalMemoryStatusEx`, IPHLPAPI, Toolhelp32, WMI (SMART/thermal), `GetSystemPowerStatus` | |
| FS watcher | `std::filesystem` + `ReadDirectoryChangesW`; `\\?\` long-path handling | |
| Jobs | Custom thread pool + scheduler, `std::stop_token` cancellation, SQLite-persisted schedules | UFR-014 |
| Logging | spdlog, wrapped in `libnexus-core` | |
| Testing | Catch2 v3 + CTest; nanobench for perf | |
| CI | GitHub Actions Windows runner: configure -> build -> ctest; clang-format, clang-tidy, cppcheck | |
| Packaging | `windeployqt` + Inno Setup (or WiX) | Auto-update deferred |
| Errors | `Result<T,E>` / `std::expected` in core; exceptions only at boundaries | UFR-020 |

**Architecture rule enforced from day one:** libs depend only downward; modules depend on libs + application-services; **modules never call each other directly**. Cross-module intelligence (spec section 9) flows through an event bus + query interfaces in application-services.

---

## 2. Repository layout

```
/nexuspc
  /apps/{desktop,agent,vault}        agent + vault are stubs until M2 / M7
  /libs/{core,db,filesystem,hash,jobs,network,system,search,crypto,notify}
  /modules/{storage,network_center,connectivity,hardware,backup,search,vault}
  /app_services/                     registry, event bus, alerts, reports, permissions, audit
  /tests/{unit,integration,system}
  /docs/{adr/, spec/}
  /cmake  /packaging  /.github/workflows
  CMakeLists.txt  CMakePresets.json  vcpkg.json  .clang-format  .clang-tidy
```

---

## 3. Phased plan

### Phase 0 — Toolchain & repo bootstrap  (~1-2 weeks)
- `git init`; `.gitignore`, LICENSE, README; move spec into `/docs/spec/`.
- Pin toolchain: VS 2022, CMake, Ninja, vcpkg (submodule + bootstrap script), Qt 6.6.
- Root CMake + `CMakePresets.json` (`windows-msvc-debug/release`) + `vcpkg.json` (sqlite3, libsodium, blake3, curl, spdlog, nlohmann-json, catch2, optionally qt6-base/qt6-charts).
- Skeleton: all empty lib targets build; `apps/desktop` opens a blank Qt window; one Catch2 test runs under CTest.
- CI workflow green.
- ADR-0001 (stack), ADR-0002 (process model + platform scope), coding-standards doc.
- 1-hour IPC spike (named-pipe echo) to de-risk the M7 vault split early.
- **Exit:** `cmake --preset` + build + ctest green in CI; window launches.

### Phase 1 — Milestone 1: Platform shell  (~4-6 weeks)
- `libnexus-core`: IDs (UUIDv7), time, `Result`, settings (JSON + schema + change signals), logging wrapper, serialization.
- `libnexus-db`: connection/statement/transaction RAII, `schema_migrations` runner, repository base. Core tables (spec section 7): `app_settings, jobs, job_runs, notifications, audit_logs, reports, machines`. Tests on in-memory DB.
- `libnexus-jobs`: thread pool, scheduler (interval / one-shot / cron-ish), cancellation, progress struct, retry/backoff, persistence to `jobs`/`job_runs`. Tests with an injectable clock.
- `libnexus-notify`: in-process notification center + tray/toast surface.
- `app_services`: `ModuleRegistry` (enable/disable — UFR-002), service locator, event bus, `AlertCenter`, `ReportCenter`, `PermissionBroker` stub, `AuditLog` writer (UFR-007).
- `apps/desktop`: main window + left-nav shell with all spec section-8 pages as empty panels; Home scaffold; Settings (module toggles, per-module retention — UFR-010); Alerts panel; Reports panel.
- **Exit:** toggle modules; schedule a dummy recurring job; see its run history after an app restart (UFR-014); fire a test notification; view an audit entry.

### Phase 2 — Milestone 2: Hardware + Connectivity  (~6-8 weeks) — first real user value
- `libnexus-system` (Windows provider): CPU total/per-core, RAM, per-disk IO + free space, NIC throughput, process list w/ per-proc CPU/RAM, battery. Sampler job -> `hardware_components, metric_samples, process_samples, thresholds`.
- `libnexus-net`: ICMP / TCP-connect / HTTP probes, DNS check, jitter + packet-loss over rolling window, outage state machine. -> `probe_targets, connectivity_samples, speed_tests, outages`. Speed test = timed download from a configurable URL (document that it is a trend, not a benchmark).
- Modules: Hardware/System Health, Connectivity. UI: PERFORMANCE page (live Qt Charts + process table), INTERNET page (connection, uptime %, outage log, latency/jitter chart, speed history).
- Threshold alerts -> `AlertCenter` (UFR-005). Reports: system diagnostic + internet reliability, export HTML/PDF/CSV (UFR-016).
- **Exit:** real updating graphs; alert fires on threshold breach; unplugging the NIC records an outage; reports export.
- **Portfolio-viable checkpoint (~3-4 months in).** Consider tagging a "NexusPC Lite" release here.

### Phase 3 — Milestone 3: Storage Intelligence / Duplicate Finder  (~5-7 weeks)
- `libnexus-fs`: recursive walker, reusable exclusion rules (UFR-008 — shared by scan/index/backup), metadata capture, reparse-point/symlink policy, `ReadDirectoryChangesW` watcher.
- `libnexus-hash`: BLAKE3 streaming + SHA-256, buffered IO, verification API.
- Dedup pipeline: size-group -> partial hash -> full hash -> `file_scans, scanned_files, duplicate_groups`. CPU/disk throttling (UFR-017), cancellation, progress.
- Safe delete: `IFileOperation` -> Recycle Bin (reversible — UFR-013), optional quarantine folder, dry-run, audit every deletion.
- UI: STORAGE page (usage bars/treemap, duplicate groups, reclaimable estimate, cleanup history) + cleanup report.
- **Exit:** scan a real drive -> correct groups -> reclaim to Recycle Bin -> undo -> cleanup report.

### Phase 4 — Milestone 4: Backup & Recovery  (~7-9 weeks)
- Reuse walker + hashing. Content-addressed store keyed by BLAKE3; full + incremental snapshots -> `backup_jobs, snapshots, snapshot_files, restore_jobs`.
- Scheduling via `libnexus-jobs`; retention/pruning (keep-N or GFS); verification pass (re-hash stored blobs); restore (full + single-file, to alternate location). Local + SMB/UNC targets; cloud deferred.
- Cross-module: pre-backup "X GB of duplicates detected" via Storage query interface (hook #1); pause on no connectivity for network targets (hook #3).
- UI: BACKUP page (jobs, snapshot browser, retention config, restore wizard, verify status) + backup report.
- **Exit:** scheduled incremental runs; verify passes; restore a file and a full snapshot to scratch; retention prunes correctly.

### Phase 5 — Milestone 5: Local Search  (~6-8 weeks)
- `libnexus-search`: tokenizer, custom inverted index, BM25 ranking, snippets -> `indexed_files, terms, index_jobs`. Keep SQLite FTS5 as fallback/benchmark — timebox the custom index.
- Parsers: text/code/Markdown/HTML; PDF (pdfium) + DOCX as stretch. Incremental re-index driven by the fs watcher.
- UI: SEARCH page (query, filters, ranked results + snippets, index controls/status).
- Cross-module: "in latest backup?" (hook #5), duplicate-group links (hook #6).

### Phase 6 — Milestone 6: Network Center  (~5-7 weeks)
- Authorized-range discovery: user must enter CIDR ranges explicitly (consent gate, UFR-015). ARP + ICMP sweep + common-service-port TCP checks -> `networks, devices, checks, check_results`.
- Per-device availability/latency/packet-loss history, service health, alerts, user labels.
- UI: NETWORK page (device list/map, per-device uptime + latency, alerts) + network report.
- Fully implement hook #4 (PC->router vs router->internet) in `AlertCenter`.

### Phase 7 — Milestone 7: Secure Vault  (~6-8 weeks + security review) — its own security project
- Prerequisite: threat-model doc + ADR + plan for independent review.
- `libnexus-crypto`: libsodium wrapper — Argon2id, XChaCha20-Poly1305, `sodium_mlock`/secure-zero, RNG.
- Separate encrypted vault file (never the shared SQLite — spec section 7 rule), versioned format, encrypted vault backups.
- Introduce `nexuspc-vault` as a separate process (first production IPC use — revisits the in-process decision): minimal named-pipe API, independent auto-lock, clipboard-clear timeout; UI never holds plaintext beyond display.
- Features: password generator, secure notes, vault health (weak/reused/old), auto-lock.
- UI: SECURITY/VAULT page.

### Phase 8 — Hardening & packaging  (~3-4 weeks, plus continuous)
- UFR conformance pass: checklist of all 20 UFRs with test evidence.
- Crash isolation (UFR-020): module exceptions caught at the app-services boundary -> module marked degraded, not fatal.
- Job conflict manager (UFR-018): no two heavy IO jobs concurrently unless user allows.
- Installer (Inno Setup/WiX) + `windeployqt` + code signing if a cert exists.
- Performance profiling pass (scan/index/backup throughput, UI responsiveness under load).
- User docs + screenshots.

### Continuous workstreams (every phase)
Unit tests per lib; integration tests (module + db + jobs); system tests (`/tests/system`); CI-green gate; ADRs in `/docs/adr`; audit logging for every destructive/admin action (UFR-007); per-module retention enforcement job (UFR-010); clang-tidy/cppcheck, and ASan/UBSan in a supplementary Linux CI job.

---

## 4. Effort sizing (solo, full-time, rough)

| Phase | Estimate |
|---|---|
| 0 Bootstrap | 1-2 wk |
| M1 Shell | 4-6 wk |
| M2 Hardware + Connectivity | 6-8 wk |
| M3 Storage/Dedup | 5-7 wk |
| M4 Backup | 7-9 wk |
| M5 Search | 6-8 wk |
| M6 Network Center | 5-7 wk |
| M7 Vault | 6-8 wk + review |
| 8 Hardening/packaging | 3-4 wk |
| **Total** | **~11-15 months**; portfolio-viable at end of M2 (~3-4 months) |

---

## 5. Risks / watch items

1. Qt via vcpkg is a multi-hour build. Evaluate the official Qt online installer in Phase 0; record the choice in an ADR.
2. Windows temperature/sensor data is unreliable without a kernel driver. Scope temps as best-effort via WMI thermal zones; do not promise coverage.
3. From-scratch speed test is approximate and server-dependent. Frame as a reliability trend, not a benchmark.
4. Scope is large — 7 modules + shared core. Hold the line on "no module before its libs."
5. Custom search index is portfolio value but a time sink — timebox it; keep FTS5 as the escape hatch.
6. Backup correctness is unforgiving — over-invest in verification + restore tests.
7. Privilege model — SMART reads, some counters, device discovery may need elevation. Run unelevated by default; request elevation per-feature (UFR-015).
8. IPC unproven until M7 — the Phase 0 named-pipe spike is the mitigation; keep the vault API surface tiny.
9. Project 12 (Mini DB Engine) stays out (spec section 13) — SQLite only.

---

## 6. Immediate next steps

1. `git init` + `.gitignore` / LICENSE / README; move the spec to `/docs/spec/`.
2. Finalize ADR-0001 and ADR-0002 (drafts in `/docs/adr/`).
3. Stand up root CMake + `CMakePresets.json` + `vcpkg.json`.
4. Spike Qt install (vcpkg vs official installer), ~1 hr each; decide and note in an ADR.
5. Empty Qt window + one empty static lib + one Catch2 test, green in GitHub Actions (Windows).
6. Start `libnexus-core`.
