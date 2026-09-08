#pragma once

#include <QList>
#include <QPointF>
#include <QWidget>

class QChart;
class QLineSeries;
class QValueAxis;

namespace nexuspc::desktop {

/// A minimal single-series line chart. X is "seconds ago" (negative, 0 at the
/// right edge); Y is the metric value.
class ChartWidget : public QWidget {
    Q_OBJECT

public:
    ChartWidget(const QString& title, double yMin, double yMax, QWidget* parent = nullptr);

    /// Replaces the plotted points. Pass an empty list to clear. When
    /// `autoscaleY` is true the Y axis grows to fit the data (never shrinks
    /// below the constructed maximum).
    void setPoints(const QList<QPointF>& points, bool autoscaleY = false);

private:
    QChart* chart_{nullptr};
    QLineSeries* series_{nullptr};
    QValueAxis* axisX_{nullptr};
    QValueAxis* axisY_{nullptr};
    double yMax_;
};

} // namespace nexuspc::desktop
