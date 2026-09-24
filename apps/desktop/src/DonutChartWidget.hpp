#pragma once

#include <QColor>
#include <QString>
#include <QWidget>

#include <vector>

class QChart;
class QLabel;

namespace nexuspc::desktop {

/// A donut (ring) chart for a single whole-vs-parts breakdown, e.g. Storage
/// Health's used/free split - matches the Home page mockup
/// (Media/design/UI design.png). Center text overlays the hole.
class DonutChartWidget : public QWidget {
    Q_OBJECT

public:
    struct Slice {
        QString label;
        double value = 0.0;
        QColor color;
    };

    explicit DonutChartWidget(QWidget* parent = nullptr);

    /// Replaces the slices and redraws. Values are the true magnitudes
    /// (e.g. bytes) - proportions are computed automatically.
    void setSlices(const std::vector<Slice>& slices);
    /// The two lines of text drawn in the donut's hole (e.g. "14%" / "Free
    /// Space").
    void setCenterText(const QString& primary, const QString& secondary);

private:
    QChart* chart_{nullptr};
    QLabel* centerPrimary_{nullptr};
    QLabel* centerSecondary_{nullptr};
};

} // namespace nexuspc::desktop
