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
| UFR-006 | Reports accessible through one report center | Met | `ReportCenter` (`app_services/include/nexus/services/report_center.hpp`); every module with a report (`hardware`, `connectivity`, `storage`, `backup`, `network_center`, `search`) registers a generator with it; Reports page lists and generates from it | `test_report_center.cpp` |
| UFR-007 | Audit trail for destructive/admin actions | Met | `AuditLog` (`app_services/`), backed by `audit_logs`; recorded for recycle-bin deletes, module enable/disable, backup runs, network scans, search indexing, and module lifecycle failures | `test_audit_log.cpp` |
| UFR-008 | Filesystem exclusions reusable across scan/index/backup | Met | `nexus::fs::ExclusionRules` (`libs/fs/`) is the one exclusion-rule type; `DuplicateScanner`, `BackupEngine`, `SearchIndexer` all take the same `ExclusionRules` parameter | `test_exclusion_rules.cpp` |
| UFR-009 | Platform exposes common machine/storage info once | Met | `HardwareRepository` (`modules/hardware/`) is the single source the Performance page, the Home page's health summary, and the system-diagnostic report all read from - no module duplicates its own hardware sampling. `Sampler::tick()` records network-interface and battery metrics (in addition to CPU/RAM/disk) through the same `record_metrics()` path, closing a gap where the provider read that data but nothing ever recorded it | `test_hardware_repository.cpp`, `test_sampler.cpp` |
| UFR-010 | Per-module data-retention settings | Met | `retention.hardware.days` / `retention.connectivity.days` / `retention.network_center.days` / `retention.storage.keep_scans` / `retention.core.days` (job runs, notifications, reports) in `app_settings`, each read once at startup via the shared `nexus::services::retention_days_setting()` (`app_services/include/nexus/services/retention_setting.hpp`) and threaded into a scheduled prune; five independent controls in Settings | `test_retention_setting.cpp` (settings value -> parsed duration, all three modules' keys); `test_sampler.cpp`/`test_prober.cpp`/`test_device_monitor.cpp`'s `"...retention"` cases and `test_hardware_repository.cpp`'s `prune_before` case (that duration actually gates what a real module prunes); `test_job_repository.cpp`/`test_notification_repository.cpp`/`test_report_center.cpp`'s prune cases |
| UFR-011 | Vault isolated from non-vault modules | Met | ADR-0003 (`docs/adr/0003-vault-security-architecture.md`) + threat model (`docs/security/vault-threat-model.md`): `nexuspc-vault` is a separate OS process; `nexus_vault_core` is linked only by `apps/vault`; no other module, and no shared-DB table, ever holds decrypted vault data | `nexus_vault_core_tests`. `tests/integration/test_vault_process.cpp` (added 2026-09-23) spawns the real compiled `nexuspc-vault.exe` and drives create/put/get/list/export/lock/unlock over its actual named pipe with the real wire protocol - an automated, repeatable test, not the one-time manual pass this row previously described. `nexus::ipc::vault_pipe_name()`'s new `NEXUSPC_VAULT_PIPE` env override (mirroring the existing `NEXUSPC_VAULT_PATH`) is what lets it run isolated from any real vault a developer has open |
| UFR-012 | Remote management disabled by default | Met | The only IPC transport (`libnexus-ipc`) is a Windows named pipe, local-machine-only by construction; no module opens a network listener or accepts inbound connections | `nexus_ipc_tests` |
| UFR-013 | Destructive storage actions reversible where the OS permits | Met | `recycle_to_bin()` (`modules/storage/src/recycle.cpp`) uses `IFileOperation` (Recycle Bin), not permanent delete | `test_recycle.cpp` |
| UFR-014 | Scheduled jobs survive application restart | Met | Backup jobs store a `schedule` string (e.g. "every 6h"); `BackupModule::start()` parses it (`parse_schedule`) and re-registers with `ctx.scheduler` on every launch - the schedule lives in the database, not in memory | `test_backup_engine.cpp`'s job-repository coverage (mechanism); `tests/integration/test_backup_schedule_restart.cpp` (real restart: two independent `ServiceContext`/`ModuleHost`/`BackupModule` instances built one after the other against the same on-disk database file - the second run's schedule fires and produces a new snapshot with nothing re-inserting or re-arming it by hand) |
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

- **UFR-010 / UFR-014, closed 2026-09-18**: the settings-value-to-prune and
  restart-to-re-armed-schedule wiring is now exercised directly rather than
  only by manual/desktop-level checks. Retention parsing was deduplicated
  out of `hardware_module.cpp`/`connectivity_module.cpp`/
  `network_center_module.cpp` (each had its own copy of the same
  settings-key/`std::stoi`/clamp logic) into
  `nexus::services::retention_days_setting()`, which is unit-tested on its
  own (`test_retention_setting.cpp`); `Sampler`/`Prober`/`DeviceMonitor` each
  gained a test that seeds a row older than a small configured retention,
  drives real `tick()` calls past their prune-every-N-ticks threshold, and
  asserts the old row - and only the old row - is gone. UFR-014 gained
  `tests/integration/test_backup_schedule_restart.cpp`: two independent
  `ServiceContext`/`ModuleHost` instances built one after the other against
  the same on-disk database file, the second one's `BackupModule::start()`
  re-arming the persisted job's schedule with nothing re-inserting or
  re-arming it by hand. Real wall-clock waiting on the *actual* production
  scheduler cadence (hardware's is 6 minutes, connectivity/network_center's
  are 1-4 hours) remains impractical for a fast test suite and isn't what
  these tests do - they exercise the same prune/re-arm code paths directly.
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
just a module name. Re-checked 2026-09-23 against current code (see the
"2026-09-22/23 gap-closure plan" section below for what changed and why);
literally checked, these remain not implemented:

- **Performance**: "temperatures" - no code anywhere reads a sensor
  temperature; `SystemProvider`/`Sampler` cover CPU/RAM/disk/network/
  battery/process metrics but nothing thermal. Not part of the 2026-09-22
  gap-closure plan's scope (found during this doc pass, not previously
  written up here) - most consumer temperature sensors need a
  vendor-specific or WMI/driver-level read this codebase has no existing
  primitive for, so this is a real, currently-unscoped gap, not a
  one-line fix.
- **Backup & Recovery**: "One-way sync" (live-mirroring a destination to
  match a source, propagating deletions) isn't implemented - only
  snapshot-based backup exists.
- **Local Search**: "Filters" - the Search page is free-text query only, no
  file-type/date filtering UI (snippets *are* implemented). Document
  content is now searchable for `.txt`/`.md`/~30 other plain-text
  extensions and `.docx` (added 2026-09-23); PDF is explicitly deferred -
  see the gap-closure section below.

Closed since the last pass of this document (previously listed here as
gaps, now implemented - see the gap-closure section below for detail):
Connectivity Center's DNS checks and scheduled speed tests; Network
Center's device discovery was ICMP-only with no MAC/vendor information
(the spec's Network Center checklist item this closes wasn't previously
listed in this section at all - an omission in an earlier pass of this
document, not a newly-introduced gap).

## 2026-09-22/23 gap-closure plan: done

A 4-agent independent code audit (2026-09-22) checked the codebase against
`docs/spec/architecture-v1.txt` and `docs/IMPLEMENTATION_PLAN.md` directly,
rather than re-reading this document's own self-reported status. Real
completion came out to ~70-72% of spec'd scope at the time, concentrated
in the network-facing modules and one entire undelivered spec section -
section 9's cross-module "intelligence" hooks (0% built, and not
previously listed anywhere in this document's Known Gaps). A 9-phase plan
closed every verified gap except one deliberate deferral (PDF parsing).
All nine phases are complete:

- **Phase 1** - wired already-built-but-disconnected code: network/battery
  telemetry into `Sampler`/Home/Performance (see UFR-009's updated
  evidence above); single-file restore exposed in the Backup UI (the
  engine already supported it, tested, just never reachable from the
  UI); `NetworkScanner`'s ping hardcoded `nexus::net::icmp_ping` with no
  injection point (untestable without real network I/O) - now takes an
  injectable `ScanPingFn`, the same shape `DeviceMonitor` already used;
  Storage scan-history view; Search's `start()` was empty - now
  registers a report like every other module; a real-file-touching
  recycle-bin test was silently excluded from every `ctest` run via a
  `[.integration]` tag; backup schedule edits needed an app restart to
  take effect (`BackupModule::reschedule_job()` now re-arms live).
- **Phase 2** - Connectivity depth: a DNS probe kind (first multi-version
  migration in the codebase - `probe_targets`' CHECK constraint had to be
  rebuilt, SQLite can't ALTER one in place); packet loss/jitter on the
  Internet page via `nexus::net::summarize()` (existed, unit-tested, had
  zero callers); the speed test (`speed_tests` table existed since the
  first migration, was fully dead schema); TCP port checks against
  discovered network devices via `nexus::net::tcp_connect` (also existed,
  also zero callers); `nexus::net::default_gateway()` (new Win32 surface,
  `GetBestRoute`) feeding a PC/router/internet outage classification in
  `Prober` - no EventBus needed for this part, `Prober` already posts
  directly to the shared `NotificationCenter`.
- **Phase 3** - the section-9 EventBus hooks: a shared
  `app_services/include/nexus/services/events/events.hpp` header (the one
  place `storage`/`backup`/`connectivity`/`search` can depend on without
  depending on each other, per `IMPLEMENTATION_PLAN.md`'s module-isolation
  rule). Storage->Backup duplicate-space warning; Connectivity->Backup
  pause (not fail) a UNC-destination job's tick during a total outage;
  Search->Backup "is this in my latest backup?" (bridges that
  `BackupEngine` stores snapshot paths relative to each job's
  `source_root` while Search indexes absolute paths - a flat string match
  would silently never work); Search->Storage "is this a known
  duplicate?" (no such bridging needed here - both already use absolute
  paths). While adding this phase's tests, found and fixed a real,
  previously-mysterious bug: `NotificationCenter::recent()` returns
  `std::vector` by value, and several test assertions called it twice in
  one expression (`std::any_of(x.recent().begin(), x.recent().end(),
  ...)`), mixing iterators from two different temporaries - undefined
  behavior that MSVC's debug STL sometimes (not always) caught as a
  blocking "vector iterators in range are from different containers"
  dialog with no console output, which is exactly what had previously
  looked like an intermittent test-suite hang.
- **Phase 4** - a real `ReadDirectoryChangesW`-based recursive directory
  watcher (`nexus::fs::DirectoryWatcher`, `libs/fs/`) - one background
  thread per watch, debounces bursts into one callback, and surfaces
  `ERROR_NOTIFY_ENUM_DIR`/a zero-length completion as an explicit
  `Overflowed` change rather than silently dropping it. Wired into
  Storage (opt-in "auto-rescan when files change") and, in Phase 7.1,
  Search. Storage also gained a reclaimable-space usage bar.
- **Phase 5** - `ObjectStore` was already 100% `std::filesystem`-based (UNC
  paths already worked transparently) - this was a UI/validation gap, not
  an engine gap. `newBackupJob()` now offers a typed UNC destination,
  validated up front (`nexus::module::backup::
  check_destination_reachable()`) rather than failing silently on the
  first scheduled run.
- **Phase 6** - `nexus::net::arp_resolve()` (Win32 `SendARP`, deliberately
  not Npcap/WinPcap - `NetworkScanner`'s existing per-host enumeration
  already covers what a raw broadcast sweep would buy) resolves and
  stores each discovered device's MAC address, shown in the Network
  page's device table. ARP only runs for hosts that already answered a
  ping (a deliberate scope decision, not in the plan's literal text -
  `SendARP`'s own timeout for a genuinely absent host can take multiple
  seconds, and most CIDR ranges have far more absent than present hosts).
  `ConnectivityRepository` gained a persisted path-status row so
  `Prober`'s router/internet classification (Phase 2) is queryable
  structured state, not only ever a notification's text - surfaced as a
  one-line PC/router/internet status on the Internet page.
- **Phase 7** - Search gained the same watcher-driven auto-re-index
  Storage got in Phase 4 (no new "incremental" logic needed -
  `SearchIndexer::index_tree()` already skips unchanged files by
  size+mtime, so re-running it on a debounced change already is
  incremental). `.docx` text extraction: the project's first "heavy"
  third-party dependencies beyond curl/sqlite/sodium (`libzip`,
  BSD-3-Clause; `pugixml`, MIT), both resolved and built cleanly via
  vcpkg on the first try. **PDF parsing (7.3) was deliberately deferred**,
  per the plan's own explicit instruction to timebox or defer it rather
  than let it block everything after it - `pdfium`'s large prebuilt
  binaries and awkward licensing/versioning make it a materially
  different kind of dependency than `libzip`/`pugixml`. This is a live
  gap, not an oversight; see "Feature completeness" above.
- **Phase 8** - the vault's own security-reviewed track. Secure notes
  promoted from a `notes` string field on password `Entry` to a
  first-class `EntryKind::SecureNote` (the migration story for files
  written before this existed: no format-version bump, just the existing
  missing-field-tolerant JSON parsing defaulting to `Password`); the
  password-health check (`weak`/`reused`/`old`) now skips notes instead
  of flagging every one as a spuriously "weak password". A new `export`
  verb reseals the live entries to a second file under the same
  already-derived key, so an export needs the same master password to
  unlock, never touching plaintext disk outside the existing AEAD path.
  See UFR-011's updated evidence above for 8.3, the real process-IPC test.
- **Phase 9** - this document.

None of this closed the separately-tracked, still-open
`nexus_integration_tests` intermittent-hang investigation for its
ThreadPool/Scheduler-side symptom (distinct from the notification-vector
bug Phase 3 found and fixed, which was confirmed to fully resolve the
*other* binary this was observed in) - real debugger tooling (Visual
Studio attach-to-process, Application Verifier) is still the recommended
next step, not more test bisection.

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
