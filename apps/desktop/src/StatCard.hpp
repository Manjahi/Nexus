#pragma once

#include <QColor>
#include <QIcon>
#include <QWidget>

class QLabel;
class QProgressBar;

namespace nexuspc::desktop {

/// A small metric card: icon + title, a big value line, an optional
/// progress bar, and a muted sublabel - matches the Home page mockup's
/// CPU/Memory/Storage/Recovery-Readiness cards (Media/design/UI design.png).
/// Styled via Theme's "statCard" QSS class, not hardcoded here.
class StatCard : public QWidget {
    Q_OBJECT

public:
    StatCard(const QIcon& icon, const QString& title, QWidget* parent = nullptr);

    /// The big value line (e.g. "68%" or "Healthy").
    void setValue(const QString& text);
    /// Colors the value line - used for non-numeric states like Recovery
    /// Readiness's "Healthy"/"At risk" text.
    void setValueColor(const QColor& color);
    /// Shows/updates the progress bar. Pass a negative percent to hide it
    /// (e.g. for cards with no meaningful fill, like Recovery Readiness).
    void setProgress(int percent);
    void setProgressColor(const QColor& color);
    void setSublabel(const QString& text);

private:
    QLabel* valueLabel_{nullptr};
    QProgressBar* progress_{nullptr};
    QLabel* sublabel_{nullptr};
};

} // namespace nexuspc::desktop
