# Sidebar icons

One SVG per nav item, matching the approved mockups
(`Media/design/UI design.png`, `Media/design/UIdesign.png`). Sourced from
Microsoft's Fluent UI System Icons (Rounded Regular, 24px) - see
`THIRD_PARTY_NOTICES.md` for the license. `continuity.svg` is the one
exception: an original NexusPC asset (see its own header comment).

Registered via `apps/desktop/resources/icons.qrc`, loaded and recolored
at runtime by `nexuspc::desktop::theme::load_nav_icon()`
(`apps/desktop/src/Theme.cpp`) - never used directly as a static asset,
since the same source file needs a different fill color per navigation
state (muted normal, Signal Cyan selected, per the mockups).

| Nav item | File | Fluent source icon |
|---|---|---|
| Home | `home.svg` | `Home` |
| Alerts | `alert.svg` | `Alert` |
| Storage | `storage.svg` | `Storage` |
| Vault | `vault.svg` | `Lock Closed` |
| Network | `network.svg` | `Network Check` |
| Internet | `internet.svg` | `Globe` |
| Performance | `performance.svg` | `Data Bar Vertical` |
| Backup | `backup.svg` | `Arrow Sync` |
| Search | `search.svg` | `Search` |
| Reports | `reports.svg` | `Document` |
| Settings | `settings.svg` | `Settings` |
| Continuity | `continuity.svg` | (original - no Fluent equivalent exists) |

Keep this table in sync with the `addNavPage()` calls in `MainWindow`'s
constructor (`apps/desktop/src/MainWindow.cpp`) - each one takes the icon
file matching its row here.
