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
| UFR-015 | Agent operates with minimum permissions needed | Met | No feature requires elevation; network device discovery only ever scans a CIDR range the user typed in (`cidr.hpp`'s `host_addresses`, capped and never auto-triggered) - the spec's consent-gate reading of this requirement | `test_cidr.cpp` |
| UFR-016 | User can export system and module reports | Met | `ReportCenter::generate()` writes HTML/CSV to disk under the reports directory; Reports page has HTML/CSV buttons per report kind | `test_report_center.cpp` |
| UFR-017 | Resource-intensive jobs support CPU/disk/bandwidth throttling | Met | `nexus::jobs::Throttle` (`libs/jobs/`): four levels, wired into `DuplicateScanner::scan()` and `BackupEngine::run()`'s per-file loops; configurable in Settings, applied fresh per job | `test_throttle.cpp` |
| UFR-018 | Avoid running conflicting heavy jobs unless the user allows it | Met | `HeavyJobGuard` (`app_services/`) tracks active heavy jobs; every heavy-job entry point in the UI (storage scan, network scan, backup run/verify/restore, search indexing) calls `confirmHeavyJob()` first, which asks the user before proceeding if something else is already running | `test_heavy_job_guard.cpp` |
| UFR-019 | Centralized configuration and diagnostics | Met | Settings page is the one configuration surface (modules, retention, throttle); the system-diagnostic report plus the audit trail are the diagnostics surface | `test_report_center.cpp`, `test_audit_log.cpp` |
| UFR-020 | Module failures shall not crash unrelated modules | Met | `ModuleHost` isolates every `apply_migrations`/`start`/`stop` call in a try/catch, marks the module degraded, and posts to notifications + audit rather than propagating | `test_module_host.cpp`'s three crash-isolation cases (migrate/start/stop) |

## Known gaps

- **UFR-010 / UFR-014**: the *mechanism* (settings-driven retention, schedule
  persistence + re-registration) is unit-tested; the exact end-to-end wiring
  (does changing the Settings spinbox actually change what a real module
  prunes; does a real restart actually re-arm a real schedule) is currently
  verified by manual/desktop-level checks (`desktop_selftest`, UI smoke
  testing) rather than a dedicated integration test. Worth a
  `tests/integration` case if this suite grows.
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

## What's still open in Milestone 8 beyond this pass

- Installer (Inno Setup/WiX) + `windeployqt` packaging + code signing (no
  cert available - document as a manual step for whoever ships this).
- A deliberate performance-profiling pass (scan/index/backup throughput, UI
  responsiveness under load) beyond the ad hoc timing already observed
  during development.
- User-facing docs + screenshots.
