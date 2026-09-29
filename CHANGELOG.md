# Changelog

All notable changes to NexusPC are documented here. Format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow
[Semantic Versioning](https://semver.org/) once the project reaches 1.0.

## [0.1.0] - 2026-09-29

First installer release.

### Added

- Eight modules sharing one local database and one desktop app: Storage,
  Vault, Network, Internet, Performance, Backup, Search, and Continuity.
- Backup: content-addressed snapshot backups (verify + restore) and a
  one-way Mirror sync mode.
- Search: full-text indexing with extension and modified-date filters.
- Continuity: recovery-readiness scoring, four disaster scenarios backed
  by real signals, actual restore rehearsals, and an encrypted Recovery
  Capsule.
- Project homepage (`docs/index.html`), served via GitHub Pages.
- Windows installer via Inno Setup.

### Known gaps

- No code-signing certificate yet - Windows SmartScreen will warn on
  first run. See the code signing policy in `README.md`.
- Search doesn't index PDF content.
- System Health doesn't read hardware temperatures.
- Full list: `docs/UFR_CONFORMANCE.md`.

[0.1.0]: https://github.com/Manjahi/Nexus/releases/tag/v0.1.0
