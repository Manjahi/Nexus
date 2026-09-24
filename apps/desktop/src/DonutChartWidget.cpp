#include "DonutChartWidget.hpp"
#include "Theme.hpp"

#include <QBrush>
#include <QChart>
#include <QChartView>
#include <QFont>
#include <QGridLayout>
#include <QLabel>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QPieSeries>
#include <QPieSlice>
#include <QVBoxLayout>

namespace nexuspc::desktop {

DonutChartWidget::DonutChartWidget(QWidget* parent) : QWidget(parent) {
    chart_ = new QChart();
    chart_->legend()->setVisible(false);
    chart_->setMargins(QMargins(0, 0, 0, 0));
    chart_->setBackgroundRoundness(0);
    chart_->setBackgroundBrush(QBrush(QColor(theme::kSurface)));
    chart_->setBackgroundPen(QPen(Qt::NoPen));

    auto* view = new QChartView(chart_, this);
    view->setRenderHint(QPainter::Antialiasing);
    view->setStyleSheet(QStringLiteral("background: transparent;"));

    auto* overlay = new QWidget(this);
    overlay->setObjectName(QStringLiteral("donutOverlay"));
    overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto* overlayLayout = new QVBoxLayout(overlay);
    overlayLayout->setContentsMargins(0, 0, 0, 0);
    overlayLayout->setSpacing(0);

    centerPrimary_ = new QLabel(overlay);
    centerPrimary_->setAlignment(Qt::AlignCenter);
    QFont primaryFont = centerPrimary_->font();
    primaryFont.setPointSize(primaryFont.pointSize() + 6);
    primaryFont.setBold(true);
    centerPrimary_->setFont(primaryFont);
    overlayLayout->addWidget(centerPrimary_);

    centerSecondary_ = new QLabel(overlay);
    centerSecondary_->setAlignment(Qt::AlignCenter);
    QPalette pal = centerSecondary_->palette();
    pal.setColor(QPalette::WindowText, QColor(theme::kTextMuted));
    centerSecondary_->setPalette(pal);
    overlayLayout->addWidget(centerSecondary_);

    auto* layout = new QGridLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(view, 0, 0);
    layout->addWidget(overlay, 0, 0, Qt::AlignCenter);
}

void DonutChartWidget::setSlices(const std::vector<Slice>& slices) {
    chart_->removeAllSeries();

    auto* series = new QPieSeries(chart_);
    series->setHoleSize(0.65);
    for (const auto& slice : slices) {
        auto* pieSlice = series->append(slice.label, slice.value);
        pieSlice->setColor(slice.color);
        pieSlice->setBorderWidth(0);
        pieSlice->setLabelVisible(false);
    }
    chart_->addSeries(series);
}

void DonutChartWidget::setCenterText(const QString& primary, const QString& secondary) {
    centerPrimary_->setText(primary);
    centerSecondary_->setText(secondary);
}

} // namespace nexuspc::desktop
