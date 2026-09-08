#include "ChartWidget.hpp"

#include <QChart>
#include <QChartView>
#include <QLineSeries>
#include <QPainter>
#include <QValueAxis>
#include <QVBoxLayout>

#include <algorithm>

namespace nexuspc::desktop {

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

} // namespace nexuspc::desktop
