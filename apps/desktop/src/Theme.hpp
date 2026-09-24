#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

#include "nexus/notify/severity.hpp"

class QWidget;

namespace nexuspc::desktop::theme {

/// The NexusPC palette (docs/BRANDING.md). Named after the design system's
/// own tokens, not their CSS use, so a future dark-mode variant can reuse
/// the names.
inline constexpr QLatin1String kMidnight{"#0B1F33"};    ///< logo, sidebar, report headers
inline constexpr QLatin1String kNavy{"#123A56"};         ///< nav hover, elevated dark surfaces
inline constexpr QLatin1String kAction{"#176B87"};       ///< buttons, links, active controls
inline constexpr QLatin1String kCyan{"#25B7D3"};         ///< logo accent, charts, active indicators
inline constexpr QLatin1String kTeal{"#20AB8A"};         ///< healthy states, completed operations
inline constexpr QLatin1String kBackground{"#F4F7FA"};   ///< app background
inline constexpr QLatin1String kSurface{"#FFFFFF"};      ///< cards, tables, panels
inline constexpr QLatin1String kText{"#162633"};         ///< headings, normal text
inline constexpr QLatin1String kTextMuted{"#5D6F7E"};    ///< labels, timestamps, descriptions
inline constexpr QLatin1String kBorder{"#D5E0E7"};       ///< dividers, table/input borders
inline constexpr QLatin1String kSelection{"#E8F7FA"};    ///< selected rows, info panels
inline constexpr QLatin1String kRowAlternate{"#F8FAFC"}; ///< zebra-striped tables

inline constexpr QLatin1String kSuccessFg{"#147D64"};
inline constexpr QLatin1String kSuccessBg{"#E7F6F0"};
inline constexpr QLatin1String kInfoFg{"#176B87"};
inline constexpr QLatin1String kInfoBg{"#E6F4F8"};
inline constexpr QLatin1String kWarningFg{"#B76E00"};
inline constexpr QLatin1String kWarningBg{"#FFF4D8"};
inline constexpr QLatin1String kCriticalFg{"#C33D4B"};
inline constexpr QLatin1String kCriticalBg{"#FDECEE"};
inline constexpr QLatin1String kNeutralFg{"#5D6F7E"};
inline constexpr QLatin1String kNeutralBg{"#EDF2F5"};

/// The application-wide QSS, applied once via qApp->setStyleSheet().
[[nodiscard]] QString stylesheet();

/// Best-effort: colors the window's native title bar/caption to Nexus
/// Midnight via DWM (Windows 11 22000+; silently a no-op everywhere else,
/// including older Windows - the default OS caption color is used there).
/// Call once, after the window is shown (needs a realized native handle).
void apply_native_title_bar(QWidget& window);

[[nodiscard]] QColor severity_foreground(nexus::notify::Severity severity);
[[nodiscard]] QColor severity_background(nexus::notify::Severity severity);
/// "Healthy"/"Information"/"Warning"/"Critical" - never color alone, per
/// docs/BRANDING.md ("combine colour with an icon and text").
[[nodiscard]] QString severity_label(nexus::notify::Severity severity);
[[nodiscard]] QIcon severity_icon(nexus::notify::Severity severity);

/// Loads a sidebar icon from `:/nexuspc/icons/<name>` (see
/// apps/desktop/resources/icons.qrc, Media/icons/README.md) and builds a
/// QIcon with a state per navigation state: muted `kTextMuted` for Normal,
/// `kCyan` for Selected (matching the mockups' active-nav treatment),
/// and a faint `kBorder` for Disabled. The source SVGs all use "#212121"
/// as their placeholder fill/stroke color (Fluent's own default, kept as
/// the recolor target string) - recoloring is a literal byte substitution
/// on the SVG source, not a runtime shader/filter, so it works whether the
/// source icon uses `fill` (every sourced Fluent icon) or `stroke` (the
/// one hand-authored exception, continuity.svg). Rendered via QSvgRenderer
/// at `pixelSize` scaled by the widget's device pixel ratio, so it stays
/// crisp on high-DPI displays rather than being upscaled from one fixed
/// raster size.
[[nodiscard]] QIcon load_nav_icon(const QString& name, int pixelSize = 20,
                                  qreal devicePixelRatio = 1.0);

} // namespace nexuspc::desktop::theme
