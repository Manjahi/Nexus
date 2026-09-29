# NexusPC branding & design system

Source of truth for the palette, typography, and logo rules used across the
desktop app and generated HTML reports. Brand source files (logo PNGs) live
in [`Media/`](../Media/) at the repo root; the desktop app embeds working
copies of them under `apps/desktop/resources/` (see "Assets" below).

## Primary palette

| Token | Colour | Purpose |
|---|---|---|
| Nexus Midnight | `#0B1F33` | Logo, sidebar, report headers, title bar |
| Nexus Navy | `#123A56` | Navigation hover, elevated dark surfaces |
| Action Blue | `#176B87` | Buttons, links and active controls |
| Signal Cyan | `#25B7D3` | Logo accent, charts, active indicators |
| System Teal | `#20AB8A` | Healthy states, completed operations |
| App Background | `#F4F7FA` | Main application and report background |
| Surface White | `#FFFFFF` | Cards, tables and panels |
| Primary Text | `#162633` | Headings and normal text |
| Secondary Text | `#5D6F7E` | Labels, timestamps and descriptions |
| Border | `#D5E0E7` | Dividers, tables and input borders |
| Soft Cyan | `#E8F7FA` | Selected rows and information panels |
| Alternate Row | `#F8FAFC` | Zebra-striped report tables |

Signal Cyan is never used for small text on white - Action Blue is used for
links and buttons instead, for stronger contrast.

## Status colours (semantic only, never decorative)

| Status | Foreground | Soft background | Meaning |
|---|---|---|---|
| Healthy/success | `#147D64` | `#E7F6F0` | Connected, completed, recovered |
| Information | `#176B87` | `#E6F4F8` | Normal system information |
| Warning | `#B76E00` | `#FFF4D8` | Threshold approaching or attention needed |
| Critical | `#C33D4B` | `#FDECEE` | Failure, unsafe state or threshold breach |
| Neutral | `#5D6F7E` | `#EDF2F5` | Paused, unavailable or not configured |

Always combined with an icon and a text label ("Warning", "Critical",
"Healthy") - colour alone is never the only signal.

## Typography

| Usage | Font |
|---|---|
| Application interface | Segoe UI Variable, Segoe UI, sans-serif (Qt's default on Windows) |
| HTML reports | Inter, Segoe UI, Arial, sans-serif |
| Metrics, hashes and paths | Cascadia Mono, Consolas, monospace |
| Logo wordmark | Geometric/humanist semibold sans-serif |

## Logo usage

- Full wordmark (`Media/Nexus.png`): About screen, reports, website,
  documentation and installer.
- Icon only (`Media/Nexus PC icon.png`): `.exe`, taskbar, title bar, tray,
  notifications and favicon.
- Dark backgrounds: white outer N with the cyan connection (a reversed
  variant isn't generated yet - see "Not yet implemented" below).
- Don't: stretch/rotate, change cyan to a status colour, place over a busy
  background, add glow/bevel/shadow/3D, use the full wordmark at title-bar
  size, or combine the mark with a shield/gear/computer icon.

## Where this is implemented

- **App/window icon**: `apps/desktop/resources/nexuspc.ico` (multi-resolution:
  16-256px, generated from the icon mark) is compiled in as the `.exe`'s
  Windows icon (`apps/desktop/resources/nexuspc.rc`); `apps/desktop/resources/nexuspc.qrc`
  embeds the icon/wordmark PNGs for `QApplication::setWindowIcon()` and any
  in-app use.
- **Palette & component styling**: `apps/desktop/src/Theme.{hpp,cpp}` - the
  colour tokens as named constants, one QSS stylesheet applied once via
  `qApp->setStyleSheet()` in `main.cpp` (sidebar, buttons, inputs, tables,
  group boxes, progress bars), and `severity_foreground()`/
  `severity_background()`/`severity_label()`/`severity_icon()` for the
  status-colour system (used by the Alerts page).
- **Title bar**: `theme::apply_native_title_bar()` sets the Windows 11
  caption/text colour via `DwmSetWindowAttribute` (`DWMWA_CAPTION_COLOR`/
  `DWMWA_TEXT_COLOR`) - a no-op on older Windows, which keeps the OS default
  caption colour instead of NexusPC's.
- **HTML reports**: `nexus::services::report::html_document()`
  (`app_services/include/nexus/services/report_format.hpp`) wraps every
  report (system diagnostic, storage cleanup, backup, network, internet
  reliability) in the themed header/card/table CSS, with
  `report::status_class()` mapping a status word to `status-success` /
  `status-warning` / `status-critical` / `status-info`.
- **Sidebar icons**: one SVG per module in `Media/icons/` (sourced from
  Microsoft's Fluent UI System Icons, MIT-licensed, except `continuity.svg`
  which is hand-authored - see the comment in that file), embedded via
  `apps/desktop/resources/icons.qrc` and passed to `MainWindow::addNavPage()`.
- **About screen**: `MainWindow::showAboutDialog()` (a Settings-page button)
  shows the full wordmark, version, build date, and a link to the repo.
- **Zebra-striped tables in the desktop app**: `setAlternatingRowColors(true)`
  is set on every table built through `configure_table()` and on
  `networkDevicesTable_` directly.

## Not yet implemented

This is a design system, not a finished asset pipeline - the following are
deliberately deferred, not overlooked:

- **An SVG master and the full Windows icon package** (9 sizes, monochrome
  variants, tray/favicon exports) - the current `.ico` is generated directly
  from the one PNG at each target size, which is fine for now but a hand-
  tuned SVG master would look sharper at the smallest sizes (16-24px).
- **Dark-mode / reversed-logo variant** - the app doesn't have a theme
  switcher yet, so only the light palette above is implemented.
