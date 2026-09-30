#include "ChartWidget.hpp"

#include "Theme.hpp"

#include <algorithm>
#include <QChart>
#include <QChartView>
#include <QLegend>
#include <QLineSeries>
#include <QPainter>
#include <QPen>
#include <QValueAxis>
#include <QVBoxLayout>

namespace nexuspc::desktop {

namespace {
/// Cycled for each named series in setSeries() - all real BRANDING.md
/// tokens (chart/active-indicator colors), not invented hex values.
const QColor kSeriesPalette[] = {
    QColor(theme::kCyan),
    QColor(theme::kAction),
    QColor(theme::kTeal),
    QColor(theme::kNavy),
};
constexpr int kSeriesPaletteSize = 4;
} // namespace

ChartWidget::ChartWidget(const QString& title, double yMin, double yMax, QWidget* parent)
    : QWidget(parent), yMax_(yMax) {
    series_ = new QLineSeries(this);

    chart_ = new QChart();
    chart_->addSeries(series_);
    chart_->legend()->hide();
    chart_->setTitle(title);
    chart_->setMargins(QMargins(4, 4, 4, 4));

    axisX_ = new QValueAxis(this);
    axisX_->setRange(-120, 0);
    axisX_->setLabelFormat("%.0fs");
    axisX_->setTickCount(5);

    axisY_ = new QValueAxis(this);
    axisY_->setRange(yMin, yMax);

    chart_->addAxis(axisX_, Qt::AlignBottom);
    chart_->addAxis(axisY_, Qt::AlignLeft);
    series_->attachAxis(axisX_);
    series_->attachAxis(axisY_);

    auto* view = new QChartView(chart_, this);
    view->setRenderHint(QPainter::Antialiasing);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(view);
}

void ChartWidget::setPoints(const QList<QPointF>& points, bool autoscaleY) {
    series_->replace(points);

    if (!points.isEmpty()) {
        double minX = points.front().x();
        double maxX = points.front().x();
        double maxY = yMax_;
        for (const QPointF& p : points) {
            minX = std::min(minX, p.x());
            maxX = std::max(maxX, p.x());
            maxY = std::max(maxY, p.y());
        }
        axisX_->setRange(minX, std::max(maxX, minX + 1.0));
        if (autoscaleY) {
            axisY_->setRange(axisY_->min(), maxY * 1.1);
        }
    }
}

void ChartWidget::setSeries(const QList<QPair<QString, QList<QPointF>>>& series, bool autoscaleY) {
    QStringList names;
    names.reserve(series.size());
    for (const auto& entry : series) {
        names << entry.first;
    }

    if (names != seriesNames_) {
        if (seriesNames_.isEmpty() && namedSeries_.isEmpty() && series_ != nullptr) {
            // First call on this instance: this chart is switching out of
            // single-series mode for good, so the default series is no
            // longer wanted.
            chart_->removeSeries(series_);
        }
        for (auto* s : namedSeries_) {
            chart_->removeSeries(s);
            delete s;
        }
        namedSeries_.clear();
        seriesNames_ = names;

        for (int i = 0; i < names.size(); ++i) {
            auto* s = new QLineSeries(this);
            s->setName(names[i]);
            QPen pen = s->pen();
            pen.setColor(kSeriesPalette[i % kSeriesPaletteSize]);
            pen.setWidth(2);
            s->setPen(pen);
            chart_->addSeries(s);
            s->attachAxis(axisX_);
            s->attachAxis(axisY_);
            namedSeries_.append(s);
        }
        chart_->legend()->setVisible(true);
        chart_->legend()->setAlignment(Qt::AlignBottom);
    }

    bool first = true;
    double minX = 0.0;
    double maxX = 0.0;
    double maxY = yMax_;
    for (int i = 0; i < series.size() && i < namedSeries_.size(); ++i) {
        const auto& points = series[i].second;
        namedSeries_[i]->replace(points);
        for (const QPointF& p : points) {
            if (first) {
                minX = maxX = p.x();
                first = false;
            }
            minX = std::min(minX, p.x());
            maxX = std::max(maxX, p.x());
            maxY = std::max(maxY, p.y());
        }
    }
    if (!first) {
        axisX_->setRange(minX, std::max(maxX, minX + 1.0));
        if (autoscaleY) {
            axisY_->setRange(axisY_->min(), maxY * 1.1);
        }
    }
}

} // namespace nexuspc::desktop
