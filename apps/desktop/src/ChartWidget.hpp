#pragma once

#include <QList>
#include <QPair>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QWidget>

class QChart;
class QLineSeries;
class QValueAxis;

namespace nexuspc::desktop {

/// A minimal line chart. X is "seconds ago" (negative, 0 at the right
/// edge); Y is the metric value. Starts in single-series mode (setPoints);
/// a chart is switched permanently into multi-series mode (setSeries) the
/// first time that's called - the two aren't meant to be mixed on one
/// instance.
class ChartWidget : public QWidget {
    Q_OBJECT

public:
    ChartWidget(const QString& title, double yMin, double yMax, QWidget* parent = nullptr);

    /// Replaces the plotted points. Pass an empty list to clear. When
    /// `autoscaleY` is true the Y axis grows to fit the data (never shrinks
    /// below the constructed maximum).
    void setPoints(const QList<QPointF>& points, bool autoscaleY = false);

    /// Multi-series variant (e.g. one line per CPU core). The legend is
    /// shown automatically once this is called. Recreates the underlying
    /// line series only when the set of names changes from the previous
    /// call, so it's safe to call every refresh tick with a stable name
    /// set - only the plotted points actually update each time.
    void setSeries(const QList<QPair<QString, QList<QPointF>>>& series, bool autoscaleY = false);

private:
    QChart* chart_{nullptr};
    QLineSeries* series_{nullptr};
    QList<QLineSeries*> namedSeries_;
    QStringList seriesNames_;
    QValueAxis* axisX_{nullptr};
    QValueAxis* axisY_{nullptr};
    double yMax_;
};

} // namespace nexuspc::desktop
