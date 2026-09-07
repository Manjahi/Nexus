#include "MainWindow.hpp"

#include <QFont>
#include <QFrame>
#include <QLabel>
#include <QListWidget>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

#include <array>
#include <utility>

namespace nexuspc::desktop {

namespace {
// Dashboard sections from the architecture spec, section 8.
constexpr std::array<std::pair<const char*, const char*>, 9> kSections{{
    {"Home", "Computer health score, storage, backup status, internet, alerts."},
    {"Storage", "Disk usage, duplicate groups, cleanup history."},
    {"Security / Vault", "Unlock vault, entries, password generator, vault health."},
    {"Network", "Device map, uptime, latency, alerts."},
    {"Internet", "Current connection, reliability, outages, speed history."},
    {"Performance", "CPU, RAM, disk IO, processes, temperatures."},
    {"Backup", "Jobs, snapshots, retention, restore."},
    {"Search", "Query bar, filters, results, indexing controls."},
    {"Reports", "Diagnostic, network, internet, storage, and backup reports."},
}};
} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("NexusPC"));
    resize(1100, 720);

    nav_ = new QListWidget(this);
    nav_->setFixedWidth(220);
    nav_->setFrameShape(QFrame::NoFrame);

    pages_ = new QStackedWidget(this);

    buildNavigation();

    connect(nav_, &QListWidget::currentRowChanged, pages_, &QStackedWidget::setCurrentIndex);
    nav_->setCurrentRow(0);

    auto* splitter = new QSplitter(this);
    splitter->addWidget(nav_);
    splitter->addWidget(pages_);
    splitter->setStretchFactor(1, 1);
    setCentralWidget(splitter);

    statusBar()->showMessage(QStringLiteral("Platform shell - Milestone 1 scaffold"));
}

void MainWindow::buildNavigation() {
    for (const auto& [name, blurb] : kSections) {
        addPage(QString::fromUtf8(name), QString::fromUtf8(blurb));
    }
}

void MainWindow::addPage(const QString& name, const QString& blurb) {
    nav_->addItem(name);

    auto* page = new QWidget(pages_);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(32, 32, 32, 32);
    layout->setSpacing(12);

    auto* title = new QLabel(name, page);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 8);
    titleFont.setBold(true);
    title->setFont(titleFont);

    auto* body = new QLabel(blurb, page);
    body->setWordWrap(true);
    body->setStyleSheet(QStringLiteral("color: palette(mid);"));

    layout->addWidget(title);
    layout->addWidget(body);
    layout->addStretch(1);

    pages_->addWidget(page);
}

} // namespace nexuspc::desktop
