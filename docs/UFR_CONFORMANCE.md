# UFR conformance

Status of the 20 unified functional requirements (spec section 10) as of
Milestone 8 hardening. "Evidence" cites the code path(s) implementing each
requirement; "Tests" cites what exercises it. This is a snapshot, not a
substitute for reading the code - re-verify before relying on it if the
referenced files have since changed.

| # | Requirement | Status | Evidence | Tests |
|---|---|---|---|---|
| UFR-001 | One dashboard for all installed modules | Met | `MainWindow` (`apps/desktop/src/MainWindow.cpp`): one nav list + `QStackedWidget` over every module's page; `buildHomePage()`/`refreshHome()` aggregate status (alerts, jobs, module list) across all of them in one place | `desktop_selftest` exercises the shared platform the dashboard reads from |
| UFR-002 | Modules independently enableable/disableable | Met | `ModuleRegistry` (`app_services/include/nexus/services/module_registry.hpp`) persists `module.<id>.enabled` in `app_settings`; Settings page checkboxes call `set_enabled()`; `ModuleHost::start_enabled()` only starts enabled modules | `tests/unit/services/test_module_registry.cpp`, `test_module_host.cpp` |
| UFR-003 | Background jobs through one job system | Met | `libnexus-jobs`' `ThreadPool` + `Scheduler` (`libs/jobs/`) are the only execution path for background work; every module's periodic worker (`Sampler`, `Prober`, `DeviceMonitor`) and every UI-triggered heavy job go through `ctx.pool`/`ctx.scheduler` | `nexus_jobs_tests` |
| UFR-004 | Long-running jobs support progress and cancellation | Met | `DuplicateScanner::scan`, `BackupEngine::run`/`verify`, `RestoreEngine::restore`, `SearchIndexer::index_tree`, `NetworkScanner::scan` all take a progress callback and a `std::function<bool()> cancelled`; Storage/Network pages wire a cancel button to an `atomic<bool>` | `test_duplicate_scanner.cpp`, `test_backup_engine.cpp`, `test_search_indexer.cpp` |
| UFR-005 | Modules publish alerts through one notification center | Met | `nexus::notify::NotificationCenter` (`libs/notify/`) is the only alert channel; `Sampler`, `Prober`, `DeviceMonitor`, and `ModuleHost` (on a lifecycle failure) all post through `ctx.notifications`; Alerts page renders it | `nexus_notify_tests`, `test_module_host.cpp`'s crash-isolation cases |
| UFR-006 | Reports accessible through one report center | Met | `ReportCenter` (`app_services/include/nexus/services/report_center.hpp`); every module with a report (`hardware`, `connectivity`, `storage`, `backup`, `network_center`) registers a generator with it; Reports page lists and generates from it | `test_report_center.cpp` |
| UFR-007 | Audit trail for destructive/admin actions | Met | `AuditLog` (`app_services/`), backed by `audit_logs`; recorded for recycle-bin deletes, module enable/disable, backup runs, network scans, search indexing, and module lifecycle failures | `test_audit_log.cpp` |
| UFR-008 | Filesystem exclusions reusable across scan/index/backup | Met | `nexus::fs::ExclusionRules` (`libs/fs/`) is the one exclusion-rule type; `DuplicateScanner`, `BackupEngine`, `SearchIndexer` all take the same `ExclusionRules` parameter | `test_exclusion_rules.cpp` |
| UFR-009 | Platform exposes common machine/storage info once | Met | `HardwareRepository` (`modules/hardware/`) is the single source the Performance page, the Home page's health summary, and the system-diagnostic report all read from - no module duplicates its own hardware sampling | `test_hardware_repository.cpp` |
| UFR-010 | Per-module data-retention settings | Met | `retention.hardware.days` / `retention.connectivity.days` / `retention.network_center.days` in `app_settings`, each read once at its module's `start()` and threaded into `Sampler`/`Prober`/`DeviceMonitor`'s `prune_before()` cutoff; three independent controls in Settings | `test_hardware_repository.cpp`'s `prune_before` case (mechanism); retention *value* wiring is settings-plumbing, not independently unit-tested - see Known gaps |
| UFR-011 | Vault isolated from non-vault modules | Met | ADR-0003 (`docs/adr/0003-vault-security-architecture.md`) + threat model (`docs/security/vault-threat-model.md`): `nexuspc-vault` is a separate OS process; `nexus_vault_core` is linked only by `apps/vault`; no other module, and no shared-DB table, ever holds decrypted vault data | `nexus_vault_core_tests`, plus an end-to-end pass against the real `nexuspc-vault.exe` over its actual named pipe |
| UFR-012 | Remote management disabled by default | Met | The only IPC transport (`libnexus-ipc`) is a Windows named pipe, local-machine-only by construction; no module opens a network listener or accepts inbound connections | `nexus_ipc_tests` |
| UFR-013 | Destructive storage actions reversible where the OS permits | Met | `recycle_to_bin()` (`modules/storage/src/recycle.cpp`) uses `IFileOperation` (Recycle Bin), not permanent delete | `test_recycle.cpp` |
| UFR-014 | Scheduled jobs survive application restart | Met | Backup jobs store a `schedule` string (e.g. "every 6h"); `BackupModule::start()` parses it (`parse_schedule`) and re-registers with `ctx.scheduler` on every launch - the schedule lives in the database, not in memory | `test_backup_engine.cpp`'s job-repository coverage; the re-scheduling call itself is exercised by `desktop_selftest`'s platform bring-up, not asserted directly - see Known gaps |
| UFR-015 | Agent operates with minimum permissions needed | Met | No feature requires elevation - every scan/index/backup/probe runs at the user's own privilege level; the installer defaults to a per-user, no-admin install (`packaging/windows/NexusPC.iss`) | manual verification (no feature has ever needed an elevation prompt) |
| UFR-016 | User can export system and module reports | Met | `ReportCenter::generate()` writes HTML/CSV to disk under the reports directory; Reports page has HTML/CSV buttons per report kind | `test_report_center.cpp` |
| UFR-017 | Resource-intensive jobs support CPU/disk/bandwidth throttling | Met | `nexus::jobs::Throttle` (`libs/jobs/`): four levels, wired into `DuplicateScanner::scan()` and `BackupEngine::run()`'s per-file loops; configurable in Settings, applied fresh per job | `test_throttle.cpp` |
| UFR-018 | Avoid running conflicting heavy jobs unless the user allows it | Met | `HeavyJobGuard` (`app_services/`) tracks active heavy jobs; every heavy-job entry point in the UI (storage scan, network scan, backup run/verify/restore, search indexing) calls `confirmHeavyJob()` first, which asks the user before proceeding if something else is already running | `test_heavy_job_guard.cpp` |
| UFR-019 | Centralized configuration and diagnostics | Met | Settings page is the one configuration surface (modules, retention, throttle); the system-diagnostic report plus the audit trail are the diagnostics surface | `test_report_center.cpp`, `test_audit_log.cpp` |
| UFR-020 | Module failures shall not crash unrelated modules | Met | `ModuleHost` isolates every `apply_migrations`/`start`/`stop` call in a try/catch, marks the module degraded, and posts to notifications + audit rather than propagating | `test_module_host.cpp`'s three crash-isolation cases (migrate/start/stop) |

Separately from any numbered UFR: network device discovery never scans on
its own initiative - `NetworkScanner` only ever walks a CIDR range the user
typed into the Network page themselves (`cidr.hpp`'s `host_addresses`,
capped at 1024 hosts). No module probes a range, port, or host the user
didn't explicitly name.

## Known gaps

- **UFR-010 / UFR-014**: the *mechanism* (settings-driven retention, schedule
  persistence + re-registration) is unit-tested; the exact end-to-end wiring
  (does changing the Settings spinbox actually change what a real module
  prunes; does a real restart actually re-arm a real schedule) is currently
  verified by manual/desktop-level checks (`desktop_selftest`, UI smoke
  testing) rather than a dedicated integration test - `tests/integration`
  now exists (`test_platform_bringup.cpp`) and covers the general "real
  module against a real Database/ThreadPool/Scheduler" shape; extending it
  to retention/schedule specifically would close this one.
- **UFR-017**: throttling covers the two heaviest disk-IO loops (storage
  scan's hashing, backup's file copy). Search indexing and network discovery
  are lighter-weight by nature (text parsing; ICMP round-trips already
  self-pace) and were judged not worth the added complexity for v1 - revisit
  if either becomes a real contention source in practice.
- **UFR-019** ("diagnostics") is satisfied by the existing report/audit
  surfaces rather than a dedicated single "Diagnostics" page. If a future
  reviewer expects one unified diagnostics screen (vs. Settings + Reports +
  Alerts together covering the ground), that's a product decision, not a
  code gap.
- This table is Windows-only evidence (the shipped platform, per
  ADR-0002) - none of it has been re-verified on another OS.

## Data retention & process-lifecycle gaps (found in a post-M8 audit, 2026-09-16)

Real gaps, verified directly against the code rather than assumed - not
correctness bugs (everything implemented is tested and works), but places
where "done" modules don't fully close the loop on cleaning up after
themselves:

- **Backup's content-addressed store never reclaims space.**
  `BackupRepository::prune_snapshots()` correctly deletes old `snapshots`/
  `snapshot_files` rows (cascading via `ON DELETE CASCADE`), but
  `ObjectStore` (`modules/backup/include/nexus/module/backup/object_store.hpp`)
  has no delete or garbage-collection method at all - only `put_file`,
  `contains`, `extract_to`, `verify`, `path_for`. A pruned snapshot's unique
  blobs stay on disk forever. Backup's own "keep N snapshots" retention
  promise doesn't reclaim the bytes it implies.
- **Storage scan history is never pruned.**
  `StorageRepository::prune_scans_keeping()` exists and is unit-tested, but
  is never called from `StorageModule` or anywhere in the desktop UI.
  Storage has no background worker (scans are on-demand only), so nothing
  currently calls it on any cadence. `file_scans`/`duplicate_groups`/
  `scanned_files` grow without bound.
- **`notifications`, `job_runs`, and generated report files/rows are never
  pruned anywhere.** Unlike the four tables UFR-010 covers, these three have
  no retention setting and no pruning code at all - every notification,
  every job run, and every report ever generated (one HTML file per click,
  and one per `desktop_selftest` run) accumulates on disk/in the DB
  indefinitely. `audit_logs` is also unpruned, which is a defensible default
  for an audit trail specifically, but it's an implicit choice, not a
  documented one.
- **Search's re-index isn't actually incremental.** `SearchIndexer::
  index_tree()` has no mtime check - "Index a folder…" re-reads and
  re-tokenizes every file every time, whether it changed or not.
  `SearchIndexer::remove_path()` exists but nothing calls it, so a file
  deleted or moved after indexing stays in search results as a dead link.
  There is no filesystem watcher (the plan's original Phase 5 design
  called for reusing one; that was deferred and never revisited).
- **`nexuspc-vault.exe` outlives the UI with no explanation.** `VaultClient`
  spawns it via `QProcess::startDetached()` with no lifecycle follow-up -
  closing NexusPC normally leaves the vault process running indefinitely
  (it will auto-lock after 5 idle minutes, but it keeps running). Only the
  installer's uninstall step ever kills it (`taskkill /IM nexuspc-vault.exe`).
- **The vault's named pipe uses Windows' default security descriptor**
  (`CreateNamedPipeA(..., nullptr)` in `libs/ipc/src/pipe.cpp`), not an
  explicit ACL scoped to the current user's SID. The threat model's
  "multi-user isolation is out of scope" note (`docs/security/
  vault-threat-model.md`) only reasons about the vault *file's* NTFS
  permissions; the pipe is a separate kernel object with its own ACL that
  doesn't inherit from a file's permissions, and the pipe name
  (`nexuspc-vault-<username>`) is fully predictable. Worth an explicit SDDL/
  DACL restricting it to the owning user before this is treated as
  production-hardened - not yet flagged in the threat model's own review
  checklist.

## Feature completeness vs. the spec's module checklists

The spec (section 2) lists explicit function checklists per module, not
just a module name. Checked literally, three items across two modules
aren't implemented:

- **Connectivity Center**: "Scheduled speed tests" - the `speed_tests` table
  exists in the schema but nothing ever writes to it. "DNS checks" - there's
  no dedicated DNS probe kind (`ProbeKind` is Icmp/Tcp/Http only); a DNS
  failure only surfaces indirectly, as an HTTP probe error.
- **Backup & Recovery**: "One-way sync" (live-mirroring a destination to
  match a source, propagating deletions) isn't implemented - only
  snapshot-based backup exists.
- **Local Search**: "Filters" - the Search page is free-text query only, no
  file-type/date filtering UI (snippets *are* implemented).

## Milestone 8 status: done

Every item in the plan's Phase 8 is complete:

- UFR conformance pass - this document.
- Crash isolation (UFR-020), heavy-job conflict guard (UFR-018), per-module
  retention (UFR-010), job throttling (UFR-017) - see the table above.
- Performance-profiling pass - `docs/PERFORMANCE.md` (methodology, measured
  throughput for scan/backup/index, and one real fix it produced: batching
  search-indexing's DB commits).
- Installer - `packaging/windows/` (Inno Setup), verified end-to-end
  (install, launch, uninstall), not just compiled. No code-signing
  certificate is available for this project, so Windows SmartScreen will
  warn on first run; documented as a manual step for whoever ships this if
  a cert is ever obtained.
- User-facing docs with screenshots - `docs/USER_GUIDE.md`.
