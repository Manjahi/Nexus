#include "StatCard.hpp"

#include "Theme.hpp"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QProgressBar>
#include <QVBoxLayout>

namespace nexuspc::desktop {

StatCard::StatCard(const QIcon& icon, const QString& title, QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("statCard"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(6);

    auto* header = new QHBoxLayout();
    header->setSpacing(8);
    auto* iconLabel = new QLabel(this);
    iconLabel->setPixmap(icon.pixmap(22, 22));
    header->addWidget(iconLabel);
    auto* titleLabel = new QLabel(title, this);
    QPalette titlePal = titleLabel->palette();
    titlePal.setColor(QPalette::WindowText, QColor(theme::kTextMuted));
    titleLabel->setPalette(titlePal);
    header->addWidget(titleLabel);
    header->addStretch(1);
    layout->addLayout(header);

    valueLabel_ = new QLabel(this);
    QFont valueFont = valueLabel_->font();
    valueFont.setPointSize(valueFont.pointSize() + 8);
    valueFont.setBold(true);
    valueLabel_->setFont(valueFont);
    layout->addWidget(valueLabel_);

    progress_ = new QProgressBar(this);
    progress_->setRange(0, 100);
    progress_->setTextVisible(false);
    progress_->setFixedHeight(6);
    progress_->setVisible(false);
    layout->addWidget(progress_);

    sublabel_ = new QLabel(this);
    QPalette subPal = sublabel_->palette();
    subPal.setColor(QPalette::WindowText, QColor(theme::kTextMuted));
    sublabel_->setPalette(subPal);
    QFont subFont = sublabel_->font();
    subFont.setPointSize(subFont.pointSize() - 1);
    sublabel_->setFont(subFont);
    layout->addWidget(sublabel_);
}

void StatCard::setValue(const QString& text) {
    valueLabel_->setText(text);
}

void StatCard::setValueColor(const QColor& color) {
    QPalette pal = valueLabel_->palette();
    pal.setColor(QPalette::WindowText, color);
    valueLabel_->setPalette(pal);
}

void StatCard::setProgress(int percent) {
    if (percent < 0) {
        progress_->setVisible(false);
        return;
    }
    progress_->setVisible(true);
    progress_->setValue(percent);
}

void StatCard::setProgressColor(const QColor& color) {
    progress_->setStyleSheet(
        QStringLiteral("QProgressBar::chunk { background: %1; border-radius: 3px; }")
            .arg(color.name()));
}

void StatCard::setSublabel(const QString& text) {
    sublabel_->setText(text);
}

} // namespace nexuspc::desktop
