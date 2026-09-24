# NexusPC

A single Windows desktop app that brings together the handful of tools
most people end up needing separately to look after their PC: finding
wasted disk space, keeping passwords safe, knowing when the network or
internet is actually the problem, watching for a machine quietly running
out of resources, backing things up, and finding files fast. One app, one
shared history, no subscriptions, nothing leaving your computer.

## The problem it solves

Keeping a Windows PC healthy usually means juggling five or six separate,
unrelated utilities - a duplicate finder, a password manager, a ping
tool, Task Manager, a backup app, an indexed search tool - each with its
own UI, its own data, and no shared picture of the machine. NexusPC is
that picture: one app, one local database, one place to look.

## What currently works today

All seven planned areas are implemented, wired into the desktop app, and
covered by an automated test suite:

- **Storage Intelligence** - scans folders for duplicate files and lets
  you clean them up safely (Recycle Bin, not permanent delete).
- **Secure Vault** - an encrypted password/notes vault, run as its own
  isolated process for defense in depth.
- **Network Center** - discovers and monitors devices on a network range
  you specify, and alerts when a known device goes offline.
- **Internet & Connectivity** - tells you whether an issue is your PC,
  your router, or your ISP.
- **System Health** - tracks CPU, memory, disk, and process load over
  time, with threshold alerts.
- **Backup & Recovery** - scheduled, space-efficient snapshot backups
  with verify and restore.
- **Local Search** - fast full-text search across files you choose to
  index.

A guided tour of every page, with real screenshots, is in the
[user guide](docs/USER_GUIDE.md).

## What's still planned or partial

NexusPC is functionally complete for its first release, but a few things
are intentionally smaller than the long-term plan - for example, backup
currently only does snapshot-style backups (no continuous one-way folder
sync yet), Local Search doesn't have file-type/date filters or PDF
content extraction (plain text and `.docx` are indexed), and System
Health doesn't read sensor temperatures. The full, honest list of what's
done vs. what's next lives in
[`docs/UFR_CONFORMANCE.md`](docs/UFR_CONFORMANCE.md) ("Feature
completeness" and "Known gaps") and the forward-looking roadmap is in
[`docs/IMPLEMENTATION_PLAN.md`](docs/IMPLEMENTATION_PLAN.md).

## How it's structured

NexusPC is one desktop application built from independent modules - one
per feature area above - sharing a common core (a local database, a
background job system, notifications, and an audit trail), so features
stay decoupled: a bug or crash in one module doesn't take the rest of the
app down. The Secure Vault is the one exception by design - it runs as
its own separate, isolated process rather than inside the main app, so
that no other part of NexusPC ever has a path to your unlocked
passwords. The reasoning behind these choices is written up in
[`docs/spec/architecture-v1.txt`](docs/spec/architecture-v1.txt) and the
[architecture decision records](docs/adr/).

## Building and running it

Most people should just run the installer once one is published; there's
nothing to configure - it installs per-user, needs no administrator
rights, and everything runs immediately from a normal desktop shortcut.
If you want to build it from source instead, install the prerequisites
in [`docs/BUILDING.md`](docs/BUILDING.md), then run
[`get-started.bat`](get-started.bat) from the repo root - it builds and
launches the app in one step.

## What data it collects

Everything NexusPC does is local to your machine - there is no account,
no cloud sync, and nothing is ever sent off your computer. What it stores
is only what you ask it to act on (a folder you scanned, a range you
scanned, a backup destination you chose), kept in one database on your
own disk that you can inspect or delete at any time. The full plain-
language breakdown of what's collected, where it's kept, and for how
long is in [`docs/DATA_AND_PRIVACY.md`](docs/DATA_AND_PRIVACY.md).

## Security and privacy

The Secure Vault is designed as its own security boundary: a separate
process, a separate encrypted file (never mixed into the shared
database), strong modern encryption, and an auto-lock that kicks in on
its own. Network scanning is always something you explicitly ask for,
never something that happens in the background on your behalf. The full
threat model, including what's explicitly out of scope for now, is in
[`docs/security/vault-threat-model.md`](docs/security/vault-threat-model.md),
and the security design decisions are in
[ADR-0003](docs/adr/0003-vault-security-architecture.md). A requirement-
by-requirement conformance pass against the product's own security and
reliability goals is in
[`docs/UFR_CONFORMANCE.md`](docs/UFR_CONFORMANCE.md).

## Supported platforms

Windows only, by design (see
[ADR-0002](docs/adr/0002-process-model-and-platform-scope.md)) - NexusPC
is built specifically around Windows' own APIs (Explorer integration,
the Recycle Bin, named pipes, Task Manager's own performance counters)
rather than being a cross-platform app that happens to also run there.

## Where the project is heading

The core product - all seven feature areas - is done and tested, and a
2026-09-22 code audit's full 9-phase gap-closure pass (cross-module
intelligence hooks, DNS/speed-test probes, Network Center ARP, Backup UNC
targets, Search incremental indexing + `.docx`, Secure Vault notes/
export/a real process-IPC test, and more - see
[`docs/UFR_CONFORMANCE.md`](docs/UFR_CONFORMANCE.md)'s "gap-closure plan"
section) is also complete. What remains is genuinely small and mostly
either a deliberate scope decision or blocked on something only the
maintainer can decide: PDF search-content extraction (deferred - the
`pdfium` dependency it needs is materially heavier than anything else in
the project so far), a few smaller checklist items (search filters,
backup one-way sync, sensor temperatures - see "Feature completeness" in
the same doc), an independent security review of the vault, and
code-signing the installer (needs either making the repo public or a
paid signing service - a decision, not a technical blocker). The
milestone-by-milestone history and forward roadmap are in
[`docs/IMPLEMENTATION_PLAN.md`](docs/IMPLEMENTATION_PLAN.md).

## More documentation

| Topic | Doc |
|---|---|
| Using the app, with screenshots | [`docs/USER_GUIDE.md`](docs/USER_GUIDE.md) |
| Building from source | [`docs/BUILDING.md`](docs/BUILDING.md) |
| Data & privacy | [`docs/DATA_AND_PRIVACY.md`](docs/DATA_AND_PRIVACY.md) |
| Architecture & product spec | [`docs/spec/architecture-v1.txt`](docs/spec/architecture-v1.txt) |
| Architecture decisions | [`docs/adr/`](docs/adr/) |
| Requirement conformance & known gaps | [`docs/UFR_CONFORMANCE.md`](docs/UFR_CONFORMANCE.md) |
| Performance benchmarking | [`docs/PERFORMANCE.md`](docs/PERFORMANCE.md) |
| Vault threat model | [`docs/security/vault-threat-model.md`](docs/security/vault-threat-model.md) |
| Implementation plan / roadmap | [`docs/IMPLEMENTATION_PLAN.md`](docs/IMPLEMENTATION_PLAN.md) |
| Installer details | [`packaging/windows/README.md`](packaging/windows/README.md) |
| Coding standards (contributors) | [`docs/CODING_STANDARDS.md`](docs/CODING_STANDARDS.md) |
| Branding & design system | [`docs/BRANDING.md`](docs/BRANDING.md) |
