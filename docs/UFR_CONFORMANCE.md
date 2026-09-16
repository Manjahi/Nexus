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
| UFR-010 | Per-module data-retention settings | Met | `retention.hardware.days` / `retention.connectivity.days` / `retention.network_center.days` / `retention.storage.keep_scans` / `retention.core.days` (job runs, notifications, reports) in `app_settings`, each read once at startup and threaded into a scheduled prune; five independent controls in Settings | `test_hardware_repository.cpp`'s `prune_before` case, `test_job_repository.cpp`/`test_notification_repository.cpp`/`test_report_center.cpp`'s prune cases (mechanism); retention *value* wiring is settings-plumbing, not independently unit-tested - see Known gaps |
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

## Data retention & process-lifecycle gaps (found in a post-M8 audit, 2026-09-16; closed the same day)

Real gaps, verified directly against the code rather than assumed - not
correctness bugs (everything implemented was tested and worked), but places
where "done" modules didn't fully close the loop on cleaning up after
themselves. All six are now fixed:

- **Backup's content-addressed store never reclaimed space. Fixed.**
  `BackupRepository::prune_snapshots()` correctly deleted old `snapshots`/
  `snapshot_files` rows (cascading via `ON DELETE CASCADE`), but `ObjectStore`
  had no delete/GC method - a pruned snapshot's unique blobs stayed on disk
  forever. `ObjectStore::collect_garbage()` (`modules/backup/src/
  object_store.cpp`) now deletes any blob whose digest isn't in a keep-set,
  and `BackupRepository::all_referenced_digests()` supplies that set (every
  digest any remaining `snapshot_files` row still points to, across all
  jobs). Called right after `prune_snapshots()` both on a scheduled backup
  tick (`backup_module.cpp`) and the manual "back up now" path
  (`MainWindow.cpp`). It also sweeps stray temp files left by an interrupted
  `put_file`. Tests: `test_object_store.cpp`'s two `collect_garbage` cases,
  `test_backup_engine.cpp`'s prune+GC end-to-end case.
- **Storage scan history was never pruned. Fixed.**
  `StorageRepository::prune_scans_keeping()` existed and was unit-tested, but
  nothing called it. `StorageModule::start()` now schedules it hourly
  against a new `retention.storage.keep_scans` setting (default 20, a
  Settings row alongside the other three retention controls).
- **`notifications`, `job_runs`, and generated report files/rows were never
  pruned anywhere. Fixed.** Unlike the four tables the rest of UFR-010
  covers, these three had no retention setting and no pruning code. Added
  `JobRepository::prune_finished_runs_before()` (never touches a run still
  Pending/Running), `NotificationRepository::prune_before()`, and
  `ReportCenter::prune_before()` (deletes the row and the file on disk).
  `Platform` (`apps/desktop/src/Platform.cpp`) schedules all three hourly
  against a new `retention.core.days` setting (default 30, a Settings row).
  `audit_logs` stays deliberately unpruned - defensible for an audit trail,
  and now an explicit choice rather than an implicit one.
- **Search's re-index wasn't actually incremental. Fixed.** `SearchIndexer::
  index_tree()` now compares each file's size+mtime against
  `SearchRepository::all_files()` and skips re-reading/re-tokenizing
  anything unchanged (`IndexSummary::files_unchanged`); anything indexed
  under the root before but not seen on this walk (deleted or moved) is
  dropped automatically (`IndexSummary::files_removed`), so a stale entry
  no longer lingers as a dead link. `remove_path()` is still there for a
  single explicit removal. A live filesystem watcher (continuous, not
  triggered by "Index a folder…") is still not implemented - deferred, same
  as before; the incremental re-index above covers the practical cost of
  not having one.
- **`nexuspc-vault.exe` outlived the UI with no explanation. Fixed.** Added a
  `shutdown` IPC verb (locks the store, then exits the process - see
  ADR-0003's verb table) and `VaultClient::shutdown_if_running()`, called
  from `MainWindow::closeEvent()`. A no-op if the session never spawned or
  reached the vault. The installer's uninstall-time `taskkill` remains as a
  backstop for anything already left running from before this fix.
- **The vault's named pipe used Windows' default security descriptor. Fixed.**
  `PipeServer::accept()` (`libs/ipc/src/pipe.cpp`) now passes an explicit
  SDDL DACL (`D:(A;;GA;;;OW)(A;;GA;;;SY)` - owner and SYSTEM only) to
  `CreateNamedPipeA`, falling back to the default descriptor only if
  building it fails. The threat model's own review checklist should still
  be updated to note this explicitly rather than relying on this table.

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
