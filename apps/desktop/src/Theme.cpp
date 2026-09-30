#include "Theme.hpp"

#include <QApplication>
#include <QFile>
#include <QPainter>
#include <QPixmap>
#include <QStyle>
#include <QSvgRenderer>
#include <QWidget>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <dwmapi.h>
#include <windows.h>

#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif
#endif

namespace nexuspc::desktop::theme {

QString stylesheet() {
    return QStringLiteral(R"(
QMainWindow, QWidget { background: %1; color: %2; }
QToolTip { background: %3; color: %2; border: 1px solid %4; padding: 4px; }

QListWidget#nav {
    background: %5;
    color: #C9D6E0;
    border: none;
    outline: none;
    padding: 8px 0;
}
QListWidget#nav::item {
    padding: 10px 18px;
    border-left: 3px solid transparent;
}
QListWidget#nav::item:hover:!selected { background: %6; color: #FFFFFF; }
QListWidget#nav::item:selected {
    background: %7;
    color: %2;
    border-left: 3px solid %8;
    font-weight: 600;
}

QPushButton {
    background: %9;
    color: #FFFFFF;
    border: none;
    border-radius: 6px;
    padding: 7px 16px;
}
QPushButton:hover { background: %5; }
QPushButton:pressed { background: %5; }
QPushButton:disabled { background: %4; color: %10; }

QLineEdit, QPlainTextEdit, QSpinBox, QComboBox {
    background: %3;
    color: %2;
    border: 1px solid %4;
    border-radius: 5px;
    padding: 4px 6px;
    selection-background-color: %7;
    selection-color: %2;
}
QLineEdit:focus, QPlainTextEdit:focus, QSpinBox:focus, QComboBox:focus {
    border: 1px solid %9;
}

QGroupBox {
    background: %3;
    border: 1px solid %4;
    border-radius: 8px;
    margin-top: 14px;
    padding-top: 10px;
    font-weight: 600;
    color: %5;
}
QGroupBox::title {
    subcontrol-origin: margin;
    left: 10px;
    padding: 0 4px;
    color: %5;
}

QTableWidget, QTreeWidget, QListWidget {
    background: %3;
    alternate-background-color: %11;
    gridline-color: %4;
    border: 1px solid %4;
    border-radius: 6px;
    color: %2;
}
QHeaderView::section {
    background: %7;
    color: %5;
    padding: 6px;
    border: none;
    border-bottom: 2px solid %9;
    font-weight: 600;
}
QTableWidget::item:selected, QTreeWidget::item:selected {
    background: %7;
    color: %2;
}

QProgressBar {
    background: %11;
    border: 1px solid %4;
    border-radius: 5px;
    text-align: center;
    color: %2;
}
QProgressBar::chunk { background: %12; border-radius: 5px; }

QTabBar::tab { padding: 8px 14px; }
QTabBar::tab:selected { color: %9; border-bottom: 2px solid %12; }

QWidget#statCard {
    background: %3;
    border: 1px solid %4;
    border-radius: 10px;
}
QWidget#statCard QLabel { background: transparent; }
QWidget#donutOverlay, QWidget#donutOverlay QLabel { background: transparent; }
)")
        .arg(kBackground,   /*%1*/
             kText,         /*%2*/
             kSurface,      /*%3*/
             kBorder,       /*%4*/
             kMidnight,     /*%5*/
             kNavy,         /*%6*/
             kSelection,    /*%7*/
             kCyan)         /*%8*/
        .arg(kAction,       /*%9*/
             kTextMuted,    /*%10*/
             kRowAlternate, /*%11*/
             kCyan);        /*%12*/
}

void apply_native_title_bar(QWidget& window) {
#ifdef _WIN32
    const auto hwnd = reinterpret_cast<HWND>(window.winId());
    COLORREF caption = RGB(0x0B, 0x1F, 0x33);
    COLORREF text = RGB(0xFF, 0xFF, 0xFF);
    // Best-effort: fails harmlessly (returns a non-S_OK HRESULT) on anything
    // older than Windows 11 22H2, which doesn't support these attributes -
    // the OS default caption color is used there instead.
    ::DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
    ::DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &text, sizeof(text));
#else
    (void) window;
#endif
}

QColor severity_foreground(nexus::notify::Severity severity) {
    switch (severity) {
        case nexus::notify::Severity::Success:
            return QColor(kSuccessFg);
        case nexus::notify::Severity::Warning:
            return QColor(kWarningFg);
        case nexus::notify::Severity::Error:
            return QColor(kCriticalFg);
        case nexus::notify::Severity::Info:
        default:
            return QColor(kInfoFg);
    }
}

QColor severity_background(nexus::notify::Severity severity) {
    switch (severity) {
        case nexus::notify::Severity::Success:
            return QColor(kSuccessBg);
        case nexus::notify::Severity::Warning:
            return QColor(kWarningBg);
        case nexus::notify::Severity::Error:
            return QColor(kCriticalBg);
        case nexus::notify::Severity::Info:
        default:
            return QColor(kInfoBg);
    }
}

QString severity_label(nexus::notify::Severity severity) {
    switch (severity) {
        case nexus::notify::Severity::Success:
            return QStringLiteral("Healthy");
        case nexus::notify::Severity::Warning:
            return QStringLiteral("Warning");
        case nexus::notify::Severity::Error:
            return QStringLiteral("Critical");
        case nexus::notify::Severity::Info:
        default:
            return QStringLiteral("Information");
    }
}

AlertPriority priority_for(nexus::notify::Severity severity) noexcept {
    switch (severity) {
        case nexus::notify::Severity::Error:
            return AlertPriority::Critical;
        case nexus::notify::Severity::Warning:
            return AlertPriority::Moderate;
        case nexus::notify::Severity::Success:
        case nexus::notify::Severity::Info:
        default:
            return AlertPriority::Low;
    }
}

QString priority_label(AlertPriority priority) {
    switch (priority) {
        case AlertPriority::Critical:
            return QStringLiteral("Critical");
        case AlertPriority::Moderate:
            return QStringLiteral("Moderate");
        case AlertPriority::Low:
        default:
            return QStringLiteral("Low");
    }
}

QColor priority_foreground(AlertPriority priority) {
    switch (priority) {
        case AlertPriority::Critical:
            return QColor(kCriticalFg);
        case AlertPriority::Moderate:
            return QColor(kWarningFg);
        case AlertPriority::Low:
        default:
            return QColor(kNeutralFg);
    }
}

QColor priority_background(AlertPriority priority) {
    switch (priority) {
        case AlertPriority::Critical:
            return QColor(kCriticalBg);
        case AlertPriority::Moderate:
            return QColor(kWarningBg);
        case AlertPriority::Low:
        default:
            return QColor(kNeutralBg);
    }
}

QIcon severity_icon(nexus::notify::Severity severity) {
    QStyle* style = QApplication::style();
    switch (severity) {
        case nexus::notify::Severity::Success:
            return style->standardIcon(QStyle::SP_DialogApplyButton);
        case nexus::notify::Severity::Warning:
            return style->standardIcon(QStyle::SP_MessageBoxWarning);
        case nexus::notify::Severity::Error:
            return style->standardIcon(QStyle::SP_MessageBoxCritical);
        case nexus::notify::Severity::Info:
        default:
            return style->standardIcon(QStyle::SP_MessageBoxInformation);
    }
}

namespace {

QPixmap render_recolored_svg(const QByteArray& source, const QString& hex, int pixelSize,
                             qreal devicePixelRatio) {
    QByteArray recolored = source;
    recolored.replace("#212121", hex.toLatin1());

    QSvgRenderer renderer(recolored);
    const int devicePixels = static_cast<int>(pixelSize * devicePixelRatio);
    QPixmap pixmap(devicePixels, devicePixels);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter);
    painter.end();
    pixmap.setDevicePixelRatio(devicePixelRatio);
    return pixmap;
}

} // namespace

QIcon load_nav_icon(const QString& name, int pixelSize, qreal devicePixelRatio) {
    QFile file(QStringLiteral(":/nexuspc/icons/%1").arg(name));
    if (!file.open(QIODevice::ReadOnly)) {
        return QIcon();
    }
    const QByteArray source = file.readAll();

    QIcon icon;
    icon.addPixmap(render_recolored_svg(source, kTextMuted, pixelSize, devicePixelRatio),
                   QIcon::Normal, QIcon::Off);
    icon.addPixmap(render_recolored_svg(source, kCyan, pixelSize, devicePixelRatio),
                   QIcon::Selected, QIcon::Off);
    icon.addPixmap(render_recolored_svg(source, kBorder, pixelSize, devicePixelRatio),
                   QIcon::Disabled, QIcon::Off);
    return icon;
}

} // namespace nexuspc::desktop::theme
