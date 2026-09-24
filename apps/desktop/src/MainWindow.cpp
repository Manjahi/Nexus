#include "MainWindow.hpp"
#include "Theme.hpp"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPalette>
#include <QCoreApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressBar>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimeZone>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

#include "ChartWidget.hpp"
#include "DonutChartWidget.hpp"
#include "NotificationBridge.hpp"
#include "StatCard.hpp"

#include "nexus/db/settings_repository.hpp"
#include "nexus/jobs/thread_pool.hpp"
#include "nexus/notify/notification_center.hpp"
#include "nexus/notify/severity.hpp"
#include "nexus/services/audit_log.hpp"
#include "nexus/services/job_repository.hpp"
#include "nexus/jobs/thread_pool.hpp"
#include "nexus/module/backup/backup_engine.hpp"
#include "nexus/module/backup/backup_module.hpp"
#include "nexus/module/backup/network_destination.hpp"
#include "nexus/module/backup/object_store.hpp"
#include "nexus/module/backup/restore_engine.hpp"
#include "nexus/module/network_center/cidr.hpp"
#include "nexus/module/network_center/network_scanner.hpp"
#include "nexus/module/search/search_indexer.hpp"
#include "nexus/module/storage/duplicate_scanner.hpp"
#include "nexus/module/storage/recycle.hpp"
#include "nexus/module/storage/storage_module.hpp"
#include "nexus/fs/exclusion_rules.hpp"
#include "nexus/jobs/throttle.hpp"
#include "nexus/services/heavy_job_guard.hpp"
#include "nexus/services/module_registry.hpp"
#include "nexus/services/events/events.hpp"
#include "nexus/services/notification_repository.hpp"
#include "nexus/services/report_center.hpp"
#include "nexus/services/service_context.hpp"

namespace nexuspc::desktop {

namespace {

QString qstr(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

// Defined further down this file, alongside the other page builders that
// use them - forward-declared here so buildHomePage() (which comes first)
// can use them too.
QLabel* page_heading(QWidget* parent, const QString& text);
void configure_table(QTableWidget* table, const QStringList& headers);
double seconds_ago(nexus::core::Timestamp now, nexus::core::Timestamp then);
QString human_bytes(std::uint64_t bytes);

QString format_time(const nexus::core::Timestamp& tp) {
    const auto secs = static_cast<qint64>(std::chrono::system_clock::to_time_t(tp));
    return QDateTime::fromSecsSinceEpoch(secs, QTimeZone::UTC)
        .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss 'UTC'"));
}

QString format_time_short(const nexus::core::Timestamp& tp) {
    const auto secs = static_cast<qint64>(std::chrono::system_clock::to_time_t(tp));
    return QDateTime::fromSecsSinceEpoch(secs, QTimeZone::UTC).toString(QStringLiteral("HH:mm"));
}

/// A bordered white panel matching the Home page mockup's card treatment
/// (Media/design/UI design.png) - styled via Theme's "statCard" QSS class,
/// so plain content (a chart, a table, a list) reads as one of the same
/// cards StatCard itself uses for CPU/Memory/Storage/Recovery Readiness.
QWidget* make_card(QWidget* parent) {
    auto* card = new QWidget(parent);
    card->setObjectName(QStringLiteral("statCard"));
    return card;
}

} // namespace

MainWindow::MainWindow(nexus::services::ServiceContext& context, QString databasePath,
                       NotificationBridge& bridge,
                       nexus::module::backup::BackupModule* backupModule,
                       nexus::module::storage::StorageModule* storageModule, QWidget* parent)
    : QMainWindow(parent),
      ctx_(context),
      dbPath_(std::move(databasePath)),
      bridge_(bridge),
      hw_(context.db),
      conn_(context.db),
      storage_(context.db),
      network_(context.db),
      backup_(context.db),
      backupModule_(backupModule),
      storageModule_(storageModule),
      searchRepo_(context.db),
      searchIndexer_(std::make_unique<nexus::module::search::SearchIndexer>(searchRepo_)) {
    setWindowTitle(QStringLiteral("NexusPC"));
    resize(1100, 720);

    nav_ = new QListWidget(this);
    nav_->setObjectName(QStringLiteral("nav"));
    nav_->setFixedWidth(220);
    nav_->setFrameShape(QFrame::NoFrame);

    pages_ = new QStackedWidget(this);

    addNavPage(QStringLiteral("home.svg"), QStringLiteral("Home"), buildHomePage());
    addNavPage(QStringLiteral("alert.svg"), QStringLiteral("Alerts"), buildAlertsPage());
    alertsNavRow_ = nav_->count() - 1;
    addNavPage(QStringLiteral("settings.svg"), QStringLiteral("Settings"), buildSettingsPage());
    addNavPage(QStringLiteral("storage.svg"), QStringLiteral("Storage"), buildStoragePage());
    addNavPage(QStringLiteral("vault.svg"), QStringLiteral("Vault"), buildVaultPage());
    vaultNavRow_ = nav_->count() - 1;
    addNavPage(QStringLiteral("network.svg"), QStringLiteral("Network"), buildNetworkPage());
    addNavPage(QStringLiteral("internet.svg"), QStringLiteral("Internet"), buildInternetPage());
    addNavPage(QStringLiteral("performance.svg"), QStringLiteral("Performance"),
              buildPerformancePage());
    performanceNavRow_ = nav_->count() - 1;
    addNavPage(QStringLiteral("backup.svg"), QStringLiteral("Backup"), buildBackupPage());
    addNavPage(QStringLiteral("search.svg"), QStringLiteral("Search"), buildSearchPage());
    addNavPage(QStringLiteral("reports.svg"), QStringLiteral("Reports"), buildReportsPage());

    connect(nav_, &QListWidget::currentRowChanged, pages_, &QStackedWidget::setCurrentIndex);
    connect(nav_, &QListWidget::currentRowChanged, this, [this](int row) {
        // The vault process is spawned lazily, on first visit to this page -
        // never automatically at app startup (ADR-0003).
        if (row == vaultNavRow_ && !vaultStatusLoaded_) {
            vaultStatusLoaded_ = true;
            refreshVaultStatus();
        }
    });
    nav_->setCurrentRow(0);

    auto* splitter = new QSplitter(this);
    splitter->addWidget(nav_);
    splitter->addWidget(pages_);
    splitter->setStretchFactor(1, 1);
    setCentralWidget(splitter);

    statusBar()->showMessage(QStringLiteral("Platform ready"));

    connect(&bridge_, &NotificationBridge::changed, this, [this] {
        refreshAlerts();
        updateAlertsNavLabel();
        refreshHome();
    });

    auto* ticker = new QTimer(this);
    connect(ticker, &QTimer::timeout, this, [this] {
        refreshHome();
        refreshAlerts();
        updateAlertsNavLabel();
        refreshPerformance();
        refreshInternet();
        refreshReports();
    });
    ticker->start(1500);

    refreshHome();
    refreshAlerts();
    updateAlertsNavLabel();
    refreshPerformance();
    refreshInternet();
    refreshReports();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // nexuspc-vault otherwise keeps running indefinitely after the UI
    // closes (it only ever auto-locks, never exits, on its own) - a no-op
    // if this session never spawned/reached it.
    vault_.shutdown_if_running();
    QMainWindow::closeEvent(event);
}

void MainWindow::addNavPage(const QString& iconName, const QString& name, QWidget* page) {
    const QIcon icon =
        nexuspc::desktop::theme::load_nav_icon(iconName, 20, qApp->devicePixelRatio());
    nav_->addItem(new QListWidgetItem(icon, name));
    pages_->addWidget(page);
}

QWidget* MainWindow::buildHomePage() {
    auto* page = new QWidget(pages_);
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(24, 24, 24, 24);
    outer->setSpacing(16);
    outer->addWidget(page_heading(page, QStringLiteral("Home")));

    QFont cardTitleFont = page->font();
    cardTitleFont.setBold(true);

    // Row 1: top-line stat cards (Media/design/UI design.png's CPU/Memory/
    // Storage/Recovery-Readiness row, plus Network folded in alongside them
    // rather than into row 2 - keeps every StatCard the same height instead
    // of mixing them into the taller chart/donut cards below).
    auto* statsRow = new QHBoxLayout();
    statsRow->setSpacing(16);

    homeCpuCard_ = new StatCard(theme::load_nav_icon(QStringLiteral("performance.svg")),
                               QStringLiteral("CPU Usage"), page);
    homeCpuCard_->setProgressColor(QColor(theme::kCyan));
    statsRow->addWidget(homeCpuCard_);

    homeMemCard_ = new StatCard(theme::load_nav_icon(QStringLiteral("performance.svg")),
                               QStringLiteral("Memory Usage"), page);
    homeMemCard_->setProgressColor(QColor(theme::kWarningFg));
    statsRow->addWidget(homeMemCard_);

    homeStorageCard_ = new StatCard(theme::load_nav_icon(QStringLiteral("storage.svg")),
                                    QStringLiteral("Storage Free"), page);
    homeStorageCard_->setProgressColor(QColor(theme::kAction));
    statsRow->addWidget(homeStorageCard_);

    homeNetworkCard_ = new StatCard(theme::load_nav_icon(QStringLiteral("network.svg")),
                                    QStringLiteral("Network Status"), page);
    homeNetworkCard_->setProgress(-1);
    statsRow->addWidget(homeNetworkCard_);

    homeRecoveryCard_ = new StatCard(theme::load_nav_icon(QStringLiteral("continuity.svg")),
                                     QStringLiteral("Recovery Readiness"), page);
    homeRecoveryCard_->setProgress(-1);
    homeRecoveryCard_->setValue(QStringLiteral("Not available"));
    homeRecoveryCard_->setValueColor(QColor(theme::kNeutralFg));
    homeRecoveryCard_->setSublabel(QStringLiteral("Continuity Lab not yet built"));
    statsRow->addWidget(homeRecoveryCard_);

    outer->addLayout(statsRow);

    // Row 2: CPU per-core chart + Storage Health donut.
    auto* detailRow = new QHBoxLayout();
    detailRow->setSpacing(16);

    auto* cpuCard = make_card(page);
    auto* cpuCardLayout = new QVBoxLayout(cpuCard);
    cpuCardLayout->setContentsMargins(16, 14, 16, 14);
    auto* cpuCardTitle = new QLabel(QStringLiteral("CPU Usage (per core)"), cpuCard);
    cpuCardTitle->setFont(cardTitleFont);
    cpuCardLayout->addWidget(cpuCardTitle);
    homeCpuCoresChart_ = new ChartWidget(QString(), 0.0, 1.0, cpuCard);
    homeCpuCoresChart_->setMinimumHeight(180);
    cpuCardLayout->addWidget(homeCpuCoresChart_);
    detailRow->addWidget(cpuCard, 2);

    auto* storageCard = make_card(page);
    auto* storageCardLayout = new QVBoxLayout(storageCard);
    storageCardLayout->setContentsMargins(16, 14, 16, 14);
    auto* storageCardTitle = new QLabel(QStringLiteral("Storage Health"), storageCard);
    storageCardTitle->setFont(cardTitleFont);
    storageCardLayout->addWidget(storageCardTitle);

    auto* storageBody = new QHBoxLayout();
    homeStorageDonut_ = new DonutChartWidget(storageCard);
    homeStorageDonut_->setMinimumSize(140, 140);
    storageBody->addWidget(homeStorageDonut_, 1);

    auto* storageLegend = new QVBoxLayout();
    const auto add_legend_row = [storageCard, storageLegend](const QColor* dotColor,
                                                              const QString& label) -> QLabel* {
        auto* row = new QHBoxLayout();
        if (dotColor != nullptr) {
            auto* dot = new QLabel(storageCard);
            dot->setFixedSize(10, 10);
            dot->setStyleSheet(
                QStringLiteral("background: %1; border-radius: 5px;").arg(dotColor->name()));
            row->addWidget(dot);
        }
        row->addWidget(new QLabel(label, storageCard));
        row->addStretch(1);
        auto* value = new QLabel(storageCard);
        row->addWidget(value);
        storageLegend->addLayout(row);
        return value;
    };
    const QColor usedColor(theme::kAction);
    const QColor freeColor(theme::kCyan);
    homeStorageUsedLabel_ = add_legend_row(&usedColor, QStringLiteral("Used Space"));
    homeStorageFreeLabel_ = add_legend_row(&freeColor, QStringLiteral("Free Space"));
    homeStorageTotalLabel_ = add_legend_row(nullptr, QStringLiteral("Total Capacity"));
    storageLegend->addStretch(1);
    storageBody->addLayout(storageLegend, 1);
    storageCardLayout->addLayout(storageBody);
    detailRow->addWidget(storageCard, 1);

    outer->addLayout(detailRow);

    // Row 3: current metrics, top processes, active alerts.
    auto* dataRow = new QHBoxLayout();
    dataRow->setSpacing(16);

    auto* metricsCard = make_card(page);
    auto* metricsLayout = new QVBoxLayout(metricsCard);
    metricsLayout->setContentsMargins(16, 14, 16, 14);
    auto* metricsTitle = new QLabel(QStringLiteral("Current Metrics"), metricsCard);
    metricsTitle->setFont(cardTitleFont);
    metricsLayout->addWidget(metricsTitle);
    homeMetricsTable_ = new QTableWidget(0, 0, metricsCard);
    configure_table(homeMetricsTable_,
                    {QStringLiteral("Metric"), QStringLiteral("Scope"), QStringLiteral("Value")});
    metricsLayout->addWidget(homeMetricsTable_);
    dataRow->addWidget(metricsCard, 1);

    auto* processesCard = make_card(page);
    auto* processesLayout = new QVBoxLayout(processesCard);
    processesLayout->setContentsMargins(16, 14, 16, 14);
    auto* processesHeader = new QHBoxLayout();
    auto* processesTitle = new QLabel(QStringLiteral("Top Processes"), processesCard);
    processesTitle->setFont(cardTitleFont);
    processesHeader->addWidget(processesTitle);
    processesHeader->addStretch(1);
    auto* viewProcesses = new QPushButton(QStringLiteral("View all processes →"), processesCard);
    viewProcesses->setFlat(true);
    viewProcesses->setStyleSheet(
        QStringLiteral("QPushButton { background: transparent; color: %1; padding: 0; border: none; }"
                       "QPushButton:hover { text-decoration: underline; }")
            .arg(QColor(theme::kAction).name()));
    connect(viewProcesses, &QPushButton::clicked, this, [this] {
        if (performanceNavRow_ >= 0) {
            nav_->setCurrentRow(performanceNavRow_);
        }
    });
    processesHeader->addWidget(viewProcesses);
    processesLayout->addLayout(processesHeader);
    homeProcessesTable_ = new QTableWidget(0, 0, processesCard);
    configure_table(homeProcessesTable_,
                    {QStringLiteral("Process"), QStringLiteral("PID"), QStringLiteral("CPU %"),
                     QStringLiteral("Working set (MB)")});
    processesLayout->addWidget(homeProcessesTable_);
    dataRow->addWidget(processesCard, 1);

    auto* alertsCard = make_card(page);
    auto* alertsLayout = new QVBoxLayout(alertsCard);
    alertsLayout->setContentsMargins(16, 14, 16, 14);
    auto* alertsHeader = new QHBoxLayout();
    auto* alertsTitle = new QLabel(QStringLiteral("Active Alerts"), alertsCard);
    alertsTitle->setFont(cardTitleFont);
    alertsHeader->addWidget(alertsTitle);
    alertsHeader->addStretch(1);
    auto* viewAlerts = new QPushButton(QStringLiteral("View all alerts →"), alertsCard);
    viewAlerts->setFlat(true);
    viewAlerts->setStyleSheet(
        QStringLiteral("QPushButton { background: transparent; color: %1; padding: 0; border: none; }"
                       "QPushButton:hover { text-decoration: underline; }")
            .arg(QColor(theme::kAction).name()));
    connect(viewAlerts, &QPushButton::clicked, this, [this] {
        if (alertsNavRow_ >= 0) {
            nav_->setCurrentRow(alertsNavRow_);
        }
    });
    alertsHeader->addWidget(viewAlerts);
    alertsLayout->addLayout(alertsHeader);
    homeAlertsList_ = new QListWidget(alertsCard);
    homeAlertsList_->setFrameShape(QFrame::NoFrame);
    homeAlertsList_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    homeAlertsList_->setSelectionMode(QAbstractItemView::NoSelection);
    alertsLayout->addWidget(homeAlertsList_);
    dataRow->addWidget(alertsCard, 1);

    outer->addLayout(dataRow, 1);

    // Diagnostic utilities - not part of the mockup, kept below the card
    // grid so they don't compete with it visually.
    auto* buttons = new QHBoxLayout();
    auto* runJob = new QPushButton(QStringLiteral("Run heartbeat job"), page);
    connect(runJob, &QPushButton::clicked, this, &MainWindow::runHeartbeatJob);
    auto* postNote = new QPushButton(QStringLiteral("Post test notification"), page);
    connect(postNote, &QPushButton::clicked, this, &MainWindow::postTestNotification);
    buttons->addWidget(runJob);
    buttons->addWidget(postNote);
    buttons->addStretch(1);
    outer->addLayout(buttons);

    return page;
}

QWidget* MainWindow::buildSettingsPage() {
    auto* page = new QWidget(pages_);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(32, 32, 32, 32);
    layout->setSpacing(16);

    auto* heading = new QLabel(QStringLiteral("Settings"), page);
    QFont headingFont = heading->font();
    headingFont.setPointSize(headingFont.pointSize() + 8);
    headingFont.setBold(true);
    heading->setFont(headingFont);
    layout->addWidget(heading);

    auto* modulesBox = new QGroupBox(QStringLiteral("Modules"), page);
    auto* modulesLayout = new QVBoxLayout(modulesBox);
    for (const auto& info : ctx_.modules.modules()) {
        const std::string id = info.id;
        // Qt treats a lone '&' as a mnemonic marker (stripped, next char
        // underlined) - escape it so "Backup & Recovery" doesn't render as
        // "Backup  Recovery".
        QString label = QString::fromStdString(info.display_name);
        label.replace(QLatin1Char('&'), QStringLiteral("&&"));
        auto* check = new QCheckBox(label, modulesBox);
        check->setChecked(ctx_.modules.is_enabled(id));
        connect(check, &QCheckBox::toggled, this, [this, id](bool on) {
            if (ctx_.modules.set_enabled(id, on)) {
                ctx_.audit.record("module_toggle", id, on ? "enabled" : "disabled", "desktop");
            }
            refreshHome();
        });
        modulesLayout->addWidget(check);
    }
    layout->addWidget(modulesBox);

    // UFR-010: per-module retention. Each module reads its own key (once, at
    // startup) - see retention_setting() in hardware/connectivity/
    // network_center/storage's *_module.cpp.
    auto* retentionBox = new QGroupBox(QStringLiteral("Data retention"), page);
    auto* retentionLayout = new QFormLayout(retentionBox);
    const auto add_retention_row = [this, retentionBox, retentionLayout](
                                       const QString& label, const std::string& key,
                                       int default_value, const QString& suffix, int max) {
        auto* spin = new QSpinBox(retentionBox);
        spin->setRange(1, max);
        spin->setSuffix(suffix);
        bool ok = false;
        const int stored =
            QString::fromStdString(ctx_.settings.get_or(key, std::to_string(default_value)))
                .toInt(&ok);
        spin->setValue(ok ? stored : default_value);
        connect(spin, &QSpinBox::valueChanged, this,
               [this, key](int value) { ctx_.settings.set(key, std::to_string(value)); });
        retentionLayout->addRow(label, spin);
    };
    add_retention_row(QStringLiteral("Hardware samples"), "retention.hardware.days", 7,
                      QStringLiteral(" days"), 3650);
    add_retention_row(QStringLiteral("Connectivity samples"), "retention.connectivity.days", 30,
                      QStringLiteral(" days"), 3650);
    add_retention_row(QStringLiteral("Network checks"), "retention.network_center.days", 30,
                      QStringLiteral(" days"), 3650);
    add_retention_row(QStringLiteral("Storage scans to keep"), "retention.storage.keep_scans", 20,
                      QStringLiteral(" scans"), 500);
    add_retention_row(QStringLiteral("Job history, notifications, reports"),
                      "retention.core.days", 30, QStringLiteral(" days"), 3650);
    layout->addWidget(retentionBox);

    auto* note = new QLabel(
        QStringLiteral("Retention changes take effect the next time NexusPC starts."), page);
    note->setStyleSheet(QStringLiteral("color: palette(mid);"));
    layout->addWidget(note);

    // UFR-017: how much a heavy job (storage scan, backup) yields the disk/
    // CPU to the rest of the system. Read fresh at the start of each job -
    // see confirmHeavyJob()'s callers.
    auto* throttleForm = new QFormLayout();
    auto* throttleCombo = new QComboBox(page);
    throttleCombo->addItem(QStringLiteral("Unlimited"),
                           qstr(nexus::jobs::to_string(nexus::jobs::ThrottleLevel::Unlimited)));
    throttleCombo->addItem(QStringLiteral("High"),
                           qstr(nexus::jobs::to_string(nexus::jobs::ThrottleLevel::High)));
    throttleCombo->addItem(QStringLiteral("Normal"),
                           qstr(nexus::jobs::to_string(nexus::jobs::ThrottleLevel::Normal)));
    throttleCombo->addItem(QStringLiteral("Low"),
                           qstr(nexus::jobs::to_string(nexus::jobs::ThrottleLevel::Low)));
    const std::string stored_throttle = ctx_.settings.get_or(
        "throttle.level", std::string(nexus::jobs::to_string(nexus::jobs::ThrottleLevel::Unlimited)));
    const int throttle_index = throttleCombo->findData(qstr(stored_throttle));
    throttleCombo->setCurrentIndex(throttle_index >= 0 ? throttle_index : 0);
    connect(throttleCombo, &QComboBox::currentIndexChanged, this, [this, throttleCombo](int index) {
        ctx_.settings.set("throttle.level", throttleCombo->itemData(index).toString().toStdString());
    });
    throttleForm->addRow(QStringLiteral("Job throttle (I/O intensity)"), throttleCombo);
    layout->addLayout(throttleForm);

    auto* throttleNote = new QLabel(
        QStringLiteral("Slows storage scans and backups to leave more disk/CPU for everything "
                       "else (UFR-017). Applies to the next job you start."),
        page);
    throttleNote->setWordWrap(true);
    throttleNote->setStyleSheet(QStringLiteral("color: palette(mid);"));
    layout->addWidget(throttleNote);

    auto* aboutButton = new QPushButton(QStringLiteral("About NexusPC"), page);
    connect(aboutButton, &QPushButton::clicked, this, &MainWindow::showAboutDialog);
    auto* aboutBar = new QHBoxLayout();
    aboutBar->addWidget(aboutButton);
    aboutBar->addStretch(1);
    layout->addLayout(aboutBar);

    layout->addStretch(1);
    return page;
}

void MainWindow::showAboutDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("About NexusPC"));
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(32, 32, 32, 32);
    layout->setSpacing(12);

    auto* wordmark = new QLabel(&dialog);
    const QPixmap logo(QStringLiteral(":/nexuspc/wordmark.png"));
    if (!logo.isNull()) {
        wordmark->setPixmap(logo.scaledToHeight(
            48, Qt::SmoothTransformation));
    } else {
        wordmark->setText(QStringLiteral("NexusPC"));
    }
    layout->addWidget(wordmark);

    auto* version = new QLabel(
        QStringLiteral("Version %1").arg(QCoreApplication::applicationVersion()), &dialog);
    layout->addWidget(version);

    auto* buildDate =
        new QLabel(QStringLiteral("Built %1").arg(QStringLiteral(__DATE__)), &dialog);
    buildDate->setStyleSheet(QStringLiteral("color: palette(mid);"));
    layout->addWidget(buildDate);

    auto* link = new QLabel(
        QStringLiteral(
            "<a href=\"https://github.com/Manjahi/Nexus\">github.com/Manjahi/Nexus</a>"),
        &dialog);
    link->setOpenExternalLinks(true);
    layout->addWidget(link);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    layout->addWidget(buttons);

    dialog.exec();
}

QWidget* MainWindow::buildAlertsPage() {
    auto* page = new QWidget(pages_);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(32, 32, 32, 32);
    layout->setSpacing(12);

    auto* heading = new QLabel(QStringLiteral("Alerts"), page);
    QFont headingFont = heading->font();
    headingFont.setPointSize(headingFont.pointSize() + 8);
    headingFont.setBold(true);
    heading->setFont(headingFont);
    layout->addWidget(heading);

    auto* markRead = new QPushButton(QStringLiteral("Mark all read"), page);
    connect(markRead, &QPushButton::clicked, this, [this] {
        for (const auto& note : ctx_.notifications.unread()) {
            ctx_.notifications_repo.mark_read(note.id);
        }
        ctx_.notifications.mark_all_read();
        refreshAlerts();
        updateAlertsNavLabel();
        refreshHome();
    });
    alertsDetailsButton_ = new QPushButton(QStringLiteral("View details"), page);
    alertsDetailsButton_->setEnabled(false);
    connect(alertsDetailsButton_, &QPushButton::clicked, this, [this] {
        if (alertsTable_ != nullptr && alertsTable_->currentRow() >= 0) {
            showAlertDetails(alertsTable_->currentRow());
        }
    });
    alertsPriorityFilter_ = new QComboBox(page);
    alertsPriorityFilter_->addItem(QStringLiteral("All priorities"));
    alertsPriorityFilter_->addItem(theme::priority_label(theme::AlertPriority::Critical));
    alertsPriorityFilter_->addItem(theme::priority_label(theme::AlertPriority::Moderate));
    alertsPriorityFilter_->addItem(theme::priority_label(theme::AlertPriority::Low));
    connect(alertsPriorityFilter_, &QComboBox::currentIndexChanged, this,
           [this](int) { refreshAlerts(); });

    auto* bar = new QHBoxLayout();
    bar->addWidget(markRead);
    bar->addWidget(alertsDetailsButton_);
    bar->addStretch(1);
    bar->addWidget(new QLabel(QStringLiteral("Priority:"), page));
    bar->addWidget(alertsPriorityFilter_);
    layout->addLayout(bar);

    alertsTable_ = new QTableWidget(0, 5, page);
    alertsTable_->setHorizontalHeaderLabels(
        {QStringLiteral("Time"), QStringLiteral("Priority"), QStringLiteral("Severity"),
         QStringLiteral("Module"), QStringLiteral("Title")});
    alertsTable_->horizontalHeader()->setStretchLastSection(true);
    alertsTable_->verticalHeader()->setVisible(false);
    alertsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    alertsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    alertsTable_->setAlternatingRowColors(true);
    connect(alertsTable_, &QTableWidget::itemSelectionChanged, this, [this] {
        alertsDetailsButton_->setEnabled(alertsTable_->currentRow() >= 0);
    });
    connect(alertsTable_, &QTableWidget::itemDoubleClicked, this,
           [this](QTableWidgetItem* item) { showAlertDetails(item->row()); });
    layout->addWidget(alertsTable_);

    return page;
}

void MainWindow::refreshHome() {
    if (homeCpuCard_ == nullptr) {
        return;
    }

    const auto snapshot = hw_.latest_snapshot();
    const auto find_metric = [&snapshot](std::string_view metric,
                                         std::string_view scope) -> std::optional<double> {
        for (const auto& sample : snapshot) {
            if (sample.metric == metric && sample.scope == scope) {
                return sample.value;
            }
        }
        return std::nullopt;
    };

    // CPU: aggregate percent up top, per-core scopes feed both the card's
    // "N cores" sublabel and row 2's multi-series chart.
    std::vector<int> coreIndices;
    for (const auto& sample : snapshot) {
        if (sample.metric == "cpu.core") {
            bool ok = false;
            const int index = QString::fromStdString(sample.scope).toInt(&ok);
            if (ok) {
                coreIndices.push_back(index);
            }
        }
    }
    std::sort(coreIndices.begin(), coreIndices.end());

    if (const auto cpu = find_metric("cpu.total", "")) {
        homeCpuCard_->setValue(QStringLiteral("%1%").arg(*cpu * 100.0, 0, 'f', 0));
        homeCpuCard_->setProgress(static_cast<int>(*cpu * 100.0));
        homeCpuCard_->setSublabel(
            coreIndices.empty()
                ? QStringLiteral("%1 total").arg(*cpu, 0, 'f', 3)
                : QStringLiteral("%1 cores | %2 total")
                      .arg(static_cast<int>(coreIndices.size()))
                      .arg(*cpu, 0, 'f', 3));
    } else {
        homeCpuCard_->setValue(QStringLiteral("-"));
        homeCpuCard_->setProgress(0);
        homeCpuCard_->setSublabel(QStringLiteral("no samples yet"));
    }

    // Memory.
    const auto memUsedFraction = find_metric("mem.used_fraction", "");
    const auto memUsedBytes = find_metric("mem.used_bytes", "");
    const auto memTotalBytes = find_metric("mem.total_bytes", "");
    if (memUsedFraction) {
        homeMemCard_->setValue(QStringLiteral("%1%").arg(*memUsedFraction * 100.0, 0, 'f', 0));
        homeMemCard_->setProgress(static_cast<int>(*memUsedFraction * 100.0));
        homeMemCard_->setSublabel(
            memUsedBytes && memTotalBytes
                ? QStringLiteral("%1 / %2")
                      .arg(human_bytes(static_cast<std::uint64_t>(*memUsedBytes)),
                           human_bytes(static_cast<std::uint64_t>(*memTotalBytes)))
                : QString());
    } else {
        homeMemCard_->setValue(QStringLiteral("-"));
        homeMemCard_->setProgress(0);
        homeMemCard_->setSublabel(QStringLiteral("no samples yet"));
    }

    // Storage: the first disk mount reported by the sampler (typically the
    // system drive) - same best-effort "whichever comes first" choice the
    // rest of this app makes when a metric can have several scopes.
    std::string diskMount;
    for (const auto& sample : snapshot) {
        if (sample.metric == "disk.total_bytes") {
            diskMount = sample.scope;
            break;
        }
    }
    const auto diskFreeFraction = find_metric("disk.free_fraction", diskMount);
    const auto diskFreeBytes = find_metric("disk.free_bytes", diskMount);
    const auto diskTotalBytes = find_metric("disk.total_bytes", diskMount);
    if (diskFreeFraction && diskFreeBytes && diskTotalBytes) {
        homeStorageCard_->setValue(QStringLiteral("%1%").arg(*diskFreeFraction * 100.0, 0, 'f', 0));
        homeStorageCard_->setProgress(static_cast<int>(*diskFreeFraction * 100.0));
        homeStorageCard_->setSublabel(
            QStringLiteral("%1 free of %2")
                .arg(human_bytes(static_cast<std::uint64_t>(*diskFreeBytes)),
                     human_bytes(static_cast<std::uint64_t>(*diskTotalBytes))));

        const double usedBytes = *diskTotalBytes - *diskFreeBytes;
        homeStorageDonut_->setSlices(
            {DonutChartWidget::Slice{QStringLiteral("Used Space"), usedBytes,
                                     QColor(theme::kAction)},
             DonutChartWidget::Slice{QStringLiteral("Free Space"), *diskFreeBytes,
                                     QColor(theme::kCyan)}});
        homeStorageDonut_->setCenterText(
            QStringLiteral("%1%").arg(*diskFreeFraction * 100.0, 0, 'f', 0),
            QStringLiteral("Free Space"));
        homeStorageUsedLabel_->setText(human_bytes(static_cast<std::uint64_t>(usedBytes)));
        homeStorageFreeLabel_->setText(human_bytes(static_cast<std::uint64_t>(*diskFreeBytes)));
        homeStorageTotalLabel_->setText(human_bytes(static_cast<std::uint64_t>(*diskTotalBytes)));
    } else {
        homeStorageCard_->setValue(QStringLiteral("-"));
        homeStorageCard_->setProgress(0);
        homeStorageCard_->setSublabel(QStringLiteral("no samples yet"));
        homeStorageDonut_->setSlices({});
        homeStorageDonut_->setCenterText(QStringLiteral("-"), QStringLiteral("no data"));
        homeStorageUsedLabel_->setText(QStringLiteral("-"));
        homeStorageFreeLabel_->setText(QStringLiteral("-"));
        homeStorageTotalLabel_->setText(QStringLiteral("-"));
    }

    // Network: reuses the same PathStatus the Internet page's headline
    // reads, collapsed to a single word + color for the card.
    using nexus::module::connectivity::PathStatus;
    const auto path = conn_.latest_path_status();
    const PathStatus status = path ? path->status : PathStatus::Unknown;
    switch (status) {
        case PathStatus::AllOk:
            homeNetworkCard_->setValue(QStringLiteral("Connected"));
            homeNetworkCard_->setValueColor(QColor(theme::kSuccessFg));
            break;
        case PathStatus::Unknown:
            homeNetworkCard_->setValue(QStringLiteral("Unknown"));
            homeNetworkCard_->setValueColor(QColor(theme::kNeutralFg));
            break;
        default:
            homeNetworkCard_->setValue(QStringLiteral("Issues detected"));
            homeNetworkCard_->setValueColor(QColor(theme::kCriticalFg));
            break;
    }
    const auto speedTests = conn_.recent_speed_tests(1);
    homeNetworkCard_->setSublabel(
        speedTests.empty() || !speedTests.front().download_bps
            ? QStringLiteral("no speed test yet")
            : QStringLiteral("%1 Mbps down")
                  .arg(*speedTests.front().download_bps / 1'000'000.0, 0, 'f', 1));

    // Row 2: CPU per-core chart.
    const auto now = nexus::core::now();
    const auto since = now - std::chrono::seconds{120};
    QList<QPair<QString, QList<QPointF>>> coreSeries;
    for (const int core : coreIndices) {
        QList<QPointF> points;
        for (const auto& point : hw_.metric_series("cpu.core", std::to_string(core), since)) {
            points.append(QPointF(seconds_ago(now, point.at), point.value));
        }
        coreSeries.append({QStringLiteral("Core %1").arg(core), points});
    }
    homeCpuCoresChart_->setSeries(coreSeries);

    // Row 3: current metrics (every raw sample, same as the mockup), top
    // processes, and the most recent alerts.
    homeMetricsTable_->setRowCount(static_cast<int>(snapshot.size()));
    for (int row = 0; row < static_cast<int>(snapshot.size()); ++row) {
        const auto& sample = snapshot[static_cast<std::size_t>(row)];
        homeMetricsTable_->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(sample.metric)));
        homeMetricsTable_->setItem(
            row, 1,
            new QTableWidgetItem(sample.scope.empty() ? QStringLiteral("-")
                                                       : QString::fromStdString(sample.scope)));
        homeMetricsTable_->setItem(row, 2, new QTableWidgetItem(QString::number(sample.value, 'f', 3)));
    }

    const auto processes = hw_.latest_processes(8);
    homeProcessesTable_->setRowCount(static_cast<int>(processes.size()));
    for (int row = 0; row < static_cast<int>(processes.size()); ++row) {
        const auto& proc = processes[static_cast<std::size_t>(row)];
        homeProcessesTable_->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(proc.name)));
        homeProcessesTable_->setItem(row, 1, new QTableWidgetItem(QString::number(proc.pid)));
        homeProcessesTable_->setItem(
            row, 2, new QTableWidgetItem(QString::number(proc.cpu_fraction * 100.0, 'f', 1)));
        homeProcessesTable_->setItem(
            row, 3,
            new QTableWidgetItem(QString::number(
                static_cast<double>(proc.working_set_bytes) / (1024.0 * 1024.0), 'f', 1)));
    }

    homeAlertsList_->clear();
    for (const auto& note : ctx_.notifications.recent(6)) {
        auto* item = new QListWidgetItem(theme::severity_icon(note.severity),
                                         QStringLiteral("%1  ·  %2")
                                             .arg(QString::fromStdString(note.title),
                                                  format_time_short(note.created_at)));
        if (!note.is_read()) {
            QFont bold = item->font();
            bold.setBold(true);
            item->setFont(bold);
        }
        homeAlertsList_->addItem(item);
    }
}

void MainWindow::refreshAlerts() {
    if (alertsTable_ == nullptr) {
        return;
    }
    alertsRows_ = ctx_.notifications.recent(200);

    const int filterIndex = alertsPriorityFilter_ != nullptr
                                ? alertsPriorityFilter_->currentIndex()
                                : 0;
    // Combo order matches AlertPriority's declaration order, offset by one
    // for the leading "All priorities" entry.
    const std::optional<theme::AlertPriority> filter =
        filterIndex <= 0 ? std::nullopt
                         : std::make_optional(static_cast<theme::AlertPriority>(filterIndex - 1));

    alertsVisibleRows_.clear();
    for (int i = 0; i < static_cast<int>(alertsRows_.size()); ++i) {
        const auto priority = theme::priority_for(alertsRows_[static_cast<std::size_t>(i)].severity);
        if (!filter.has_value() || *filter == priority) {
            alertsVisibleRows_.push_back(i);
        }
    }

    alertsTable_->setRowCount(static_cast<int>(alertsVisibleRows_.size()));
    for (int row = 0; row < static_cast<int>(alertsVisibleRows_.size()); ++row) {
        const auto& note = alertsRows_[static_cast<std::size_t>(alertsVisibleRows_[static_cast<std::size_t>(row)])];
        const auto priority = theme::priority_for(note.severity);
        auto* time = new QTableWidgetItem(format_time(note.created_at));
        auto* priorityItem = new QTableWidgetItem(theme::priority_label(priority));
        priorityItem->setForeground(theme::priority_foreground(priority));
        priorityItem->setBackground(theme::priority_background(priority));
        auto* severity = new QTableWidgetItem(theme::severity_icon(note.severity),
                                              theme::severity_label(note.severity));
        severity->setForeground(theme::severity_foreground(note.severity));
        auto* module = new QTableWidgetItem(QString::fromStdString(note.module));
        auto* title = new QTableWidgetItem(QString::fromStdString(note.title));
        if (!note.is_read()) {
            QFont bold = time->font();
            bold.setBold(true);
            time->setFont(bold);
            priorityItem->setFont(bold);
            severity->setFont(bold);
            module->setFont(bold);
            title->setFont(bold);
        }
        alertsTable_->setItem(row, 0, time);
        alertsTable_->setItem(row, 1, priorityItem);
        alertsTable_->setItem(row, 2, severity);
        alertsTable_->setItem(row, 3, module);
        alertsTable_->setItem(row, 4, title);
    }
    if (alertsDetailsButton_ != nullptr) {
        alertsDetailsButton_->setEnabled(alertsTable_->currentRow() >= 0);
    }
}

void MainWindow::showAlertDetails(int visibleRow) {
    if (visibleRow < 0 || visibleRow >= static_cast<int>(alertsVisibleRows_.size())) {
        return;
    }
    const int row = alertsVisibleRows_[static_cast<std::size_t>(visibleRow)];
    const auto& note = alertsRows_[static_cast<std::size_t>(row)];

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Alert details"));
    dialog.setMinimumWidth(420);
    auto* layout = new QFormLayout(&dialog);

    auto* severity = new QLabel(&dialog);
    severity->setPixmap(theme::severity_icon(note.severity).pixmap(16, 16));
    auto* severityRow = new QWidget(&dialog);
    auto* severityLayout = new QHBoxLayout(severityRow);
    severityLayout->setContentsMargins(0, 0, 0, 0);
    auto* severityLabel = new QLabel(theme::severity_label(note.severity), severityRow);
    QPalette pal = severityLabel->palette();
    pal.setColor(QPalette::WindowText, theme::severity_foreground(note.severity));
    severityLabel->setPalette(pal);
    severityLayout->addWidget(severity);
    severityLayout->addWidget(severityLabel);
    severityLayout->addStretch(1);

    const auto priority = theme::priority_for(note.severity);
    auto* priorityLabel = new QLabel(theme::priority_label(priority), &dialog);
    QPalette priorityPal = priorityLabel->palette();
    priorityPal.setColor(QPalette::WindowText, theme::priority_foreground(priority));
    priorityLabel->setPalette(priorityPal);
    QFont priorityFont = priorityLabel->font();
    priorityFont.setBold(true);
    priorityLabel->setFont(priorityFont);

    layout->addRow(QStringLiteral("Time:"), new QLabel(format_time(note.created_at), &dialog));
    layout->addRow(QStringLiteral("Priority:"), priorityLabel);
    layout->addRow(QStringLiteral("Severity:"), severityRow);
    layout->addRow(QStringLiteral("Module:"), new QLabel(QString::fromStdString(note.module), &dialog));
    layout->addRow(QStringLiteral("Status:"),
                  new QLabel(note.is_read() ? QStringLiteral("Read") : QStringLiteral("Unread"),
                             &dialog));

    auto* title = new QLabel(QString::fromStdString(note.title), &dialog);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);
    title->setWordWrap(true);
    layout->addRow(QStringLiteral("Title:"), title);

    auto* body = new QLabel(
        note.body.empty() ? QStringLiteral("(no further detail)") : QString::fromStdString(note.body),
        &dialog);
    body->setWordWrap(true);
    body->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addRow(QStringLiteral("Detail:"), body);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    layout->addRow(buttons);

    dialog.exec();
}

void MainWindow::updateAlertsNavLabel() {
    if (alertsNavRow_ < 0 || nav_->item(alertsNavRow_) == nullptr) {
        return;
    }
    const auto unread = ctx_.notifications.unread_count();
    nav_->item(alertsNavRow_)
        ->setText(unread == 0 ? QStringLiteral("Alerts")
                              : QStringLiteral("Alerts (%1)").arg(static_cast<int>(unread)));
}

void MainWindow::runHeartbeatJob() {
    if (heartbeatJobId_.is_nil()) {
        nexus::services::JobRecord record;
        record.module = "platform";
        record.kind = "heartbeat";
        record.schedule = "manual";
        heartbeatJobId_ = ctx_.jobs.upsert_job(record);
    }

    const nexus::core::Uuid run_id = ctx_.jobs.start_run(heartbeatJobId_);
    ctx_.notifications.post("platform", nexus::notify::Severity::Info,
                            "Heartbeat job started");

    auto* jobs = &ctx_.jobs;
    auto* notifications = &ctx_.notifications;
    ctx_.pool.submit([jobs, notifications, run_id] {
        for (int i = 1; i <= 4; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(120));
            jobs->update_run_progress(run_id, i / 4.0, "tick " + std::to_string(i) + "/4");
        }
        jobs->finish_run(run_id, nexus::services::JobState::Succeeded);
        notifications->post("platform", nexus::notify::Severity::Success,
                            "Heartbeat job completed");
    });
}

void MainWindow::postTestNotification() {
    ctx_.notifications.post("desktop", nexus::notify::Severity::Warning, "Test notification",
                            QDateTime::currentDateTimeUtc()
                                .toString(Qt::ISODate)
                                .toStdString());
}

namespace {

QLabel* page_heading(QWidget* parent, const QString& text) {
    auto* heading = new QLabel(text, parent);
    QFont font = heading->font();
    font.setPointSize(font.pointSize() + 8);
    font.setBold(true);
    heading->setFont(font);
    return heading;
}

void configure_table(QTableWidget* table, const QStringList& headers) {
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setAlternatingRowColors(true);
}

double seconds_ago(nexus::core::Timestamp now, nexus::core::Timestamp then) {
    return -std::chrono::duration<double>(now - then).count();
}

QString human_bytes(std::uint64_t bytes) {
    static const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    return QStringLiteral("%1 %2").arg(value, 0, 'f', unit == 0 ? 0 : 1).arg(units[unit]);
}

} // namespace

QWidget* MainWindow::buildPerformancePage() {
    auto* page = new QWidget(pages_);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(12);
    layout->addWidget(page_heading(page, QStringLiteral("Performance")));

    QFont cardTitleFont = page->font();
    cardTitleFont.setBold(true);

    auto* statsRow = new QHBoxLayout();
    statsRow->setSpacing(16);
    perfCpuCard_ = new StatCard(theme::load_nav_icon(QStringLiteral("performance.svg")),
                               QStringLiteral("CPU Usage"), page);
    perfCpuCard_->setProgressColor(QColor(theme::kCyan));
    statsRow->addWidget(perfCpuCard_);
    perfMemCard_ = new StatCard(theme::load_nav_icon(QStringLiteral("performance.svg")),
                               QStringLiteral("Memory Usage"), page);
    perfMemCard_->setProgressColor(QColor(theme::kWarningFg));
    statsRow->addWidget(perfMemCard_);
    perfBatteryCard_ = new StatCard(theme::load_nav_icon(QStringLiteral("performance.svg")),
                                    QStringLiteral("Battery"), page);
    perfBatteryCard_->setProgressColor(QColor(theme::kTeal));
    statsRow->addWidget(perfBatteryCard_);
    layout->addLayout(statsRow);

    auto* chartCard = make_card(page);
    auto* chartCardLayout = new QVBoxLayout(chartCard);
    chartCardLayout->setContentsMargins(16, 14, 16, 14);
    auto* chartTitle = new QLabel(QStringLiteral("CPU load"), chartCard);
    chartTitle->setFont(cardTitleFont);
    chartCardLayout->addWidget(chartTitle);
    cpuChart_ = new ChartWidget(QString(), 0.0, 1.0, chartCard);
    cpuChart_->setMinimumHeight(180);
    chartCardLayout->addWidget(cpuChart_);
    layout->addWidget(chartCard);

    netLabel_ = new QLabel(page);
    netLabel_->setWordWrap(true);
    layout->addWidget(netLabel_);

    auto* procCard = make_card(page);
    auto* procCardLayout = new QVBoxLayout(procCard);
    procCardLayout->setContentsMargins(16, 14, 16, 14);
    auto* procTitle = new QLabel(QStringLiteral("Processes"), procCard);
    procTitle->setFont(cardTitleFont);
    procCardLayout->addWidget(procTitle);
    procTable_ = new QTableWidget(0, 0, procCard);
    configure_table(procTable_, {QStringLiteral("Process"), QStringLiteral("PID"),
                                 QStringLiteral("CPU %"), QStringLiteral("Working set (MB)")});
    procCardLayout->addWidget(procTable_);
    layout->addWidget(procCard, 1);
    return page;
}

void MainWindow::refreshPerformance() {
    if (cpuChart_ == nullptr) {
        return;
    }
    const auto now = nexus::core::now();
    const auto since = now - std::chrono::seconds{120};

    QList<QPointF> cpu;
    for (const auto& point : hw_.metric_series("cpu.total", "", since)) {
        cpu.append(QPointF(seconds_ago(now, point.at), point.value));
    }
    cpuChart_->setPoints(cpu);

    const auto snapshot = hw_.latest_snapshot();
    const auto find_metric = [&snapshot](std::string_view metric,
                                         std::string_view scope) -> std::optional<double> {
        for (const auto& sample : snapshot) {
            if (sample.metric == metric && sample.scope == scope) {
                return sample.value;
            }
        }
        return std::nullopt;
    };

    if (const auto cpuVal = find_metric("cpu.total", "")) {
        perfCpuCard_->setValue(QStringLiteral("%1%").arg(*cpuVal * 100.0, 0, 'f', 0));
        perfCpuCard_->setProgress(static_cast<int>(*cpuVal * 100.0));
        perfCpuCard_->setSublabel(QStringLiteral("%1 total").arg(*cpuVal, 0, 'f', 3));
    } else {
        perfCpuCard_->setValue(QStringLiteral("-"));
        perfCpuCard_->setProgress(0);
        perfCpuCard_->setSublabel(QStringLiteral("no samples yet"));
    }

    const auto memUsedFraction = find_metric("mem.used_fraction", "");
    const auto memUsedBytes = find_metric("mem.used_bytes", "");
    const auto memTotalBytes = find_metric("mem.total_bytes", "");
    if (memUsedFraction) {
        perfMemCard_->setValue(QStringLiteral("%1%").arg(*memUsedFraction * 100.0, 0, 'f', 0));
        perfMemCard_->setProgress(static_cast<int>(*memUsedFraction * 100.0));
        perfMemCard_->setSublabel(
            memUsedBytes && memTotalBytes
                ? QStringLiteral("%1 / %2")
                      .arg(human_bytes(static_cast<std::uint64_t>(*memUsedBytes)),
                           human_bytes(static_cast<std::uint64_t>(*memTotalBytes)))
                : QString());
    } else {
        perfMemCard_->setValue(QStringLiteral("-"));
        perfMemCard_->setProgress(0);
        perfMemCard_->setSublabel(QStringLiteral("no samples yet"));
    }

    QStringList interfaces;
    std::unordered_set<std::string> seen;
    for (const auto& sample : snapshot) {
        if (sample.metric == "net.up" && seen.insert(sample.scope).second) {
            interfaces << QStringLiteral("%1 (%2)").arg(
                QString::fromStdString(sample.scope),
                sample.value > 0.0 ? QStringLiteral("up") : QStringLiteral("down"));
        }
    }
    netLabel_->setText(interfaces.isEmpty()
                           ? QStringLiteral("Network: no interfaces")
                           : QStringLiteral("Network: %1").arg(interfaces.join(QStringLiteral(", "))));

    const auto battery_present = find_metric("battery.present", "");
    if (battery_present && *battery_present > 0.0) {
        const auto charge = find_metric("battery.charge_fraction", "").value_or(0.0);
        const auto charging = find_metric("battery.charging", "").value_or(0.0) > 0.0;
        perfBatteryCard_->setValue(QStringLiteral("%1%").arg(charge * 100.0, 0, 'f', 0));
        perfBatteryCard_->setProgress(static_cast<int>(charge * 100.0));
        perfBatteryCard_->setSublabel(charging ? QStringLiteral("Charging")
                                               : QStringLiteral("On battery"));
        perfBatteryCard_->setVisible(true);
    } else {
        perfBatteryCard_->setVisible(false);
    }

    const auto processes = hw_.latest_processes(15);
    procTable_->setRowCount(static_cast<int>(processes.size()));
    for (int row = 0; row < static_cast<int>(processes.size()); ++row) {
        const auto& proc = processes[static_cast<std::size_t>(row)];
        procTable_->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(proc.name)));
        procTable_->setItem(row, 1, new QTableWidgetItem(QString::number(proc.pid)));
        procTable_->setItem(
            row, 2, new QTableWidgetItem(QString::number(proc.cpu_fraction * 100.0, 'f', 1)));
        procTable_->setItem(row, 3,
                            new QTableWidgetItem(QString::number(
                                static_cast<double>(proc.working_set_bytes) / (1024.0 * 1024.0),
                                'f', 1)));
    }
}

QWidget* MainWindow::buildInternetPage() {
    auto* page = new QWidget(pages_);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(12);
    layout->addWidget(page_heading(page, QStringLiteral("Internet")));

    QFont cardTitleFont = page->font();
    cardTitleFont.setBold(true);

    auto* statsRow = new QHBoxLayout();
    statsRow->setSpacing(16);
    internetPathCard_ = new StatCard(theme::load_nav_icon(QStringLiteral("internet.svg")),
                                     QStringLiteral("Path Status"), page);
    internetPathCard_->setProgress(-1);
    internetPathCard_->setValue(QStringLiteral("Unknown"));
    internetPathCard_->setValueColor(QColor(theme::kNeutralFg));
    internetPathCard_->setSublabel(QStringLiteral("PC — Router — Internet: not yet known"));
    statsRow->addWidget(internetPathCard_);
    internetLatencyCard_ = new StatCard(theme::load_nav_icon(QStringLiteral("internet.svg")),
                                        QStringLiteral("Latency"), page);
    internetLatencyCard_->setProgress(-1);
    statsRow->addWidget(internetLatencyCard_);
    internetSpeedCard_ = new StatCard(theme::load_nav_icon(QStringLiteral("internet.svg")),
                                      QStringLiteral("Download Speed"), page);
    internetSpeedCard_->setProgress(-1);
    statsRow->addWidget(internetSpeedCard_);
    layout->addLayout(statsRow);

    auto* uptimeCard = make_card(page);
    auto* uptimeCardLayout = new QVBoxLayout(uptimeCard);
    uptimeCardLayout->setContentsMargins(16, 14, 16, 14);
    auto* uptimeTitle = new QLabel(QStringLiteral("Uptime by target"), uptimeCard);
    uptimeTitle->setFont(cardTitleFont);
    uptimeCardLayout->addWidget(uptimeTitle);
    uptimeTable_ = new QTableWidget(0, 0, uptimeCard);
    configure_table(uptimeTable_,
                    {QStringLiteral("Target"), QStringLiteral("Uptime (last hour)"),
                     QStringLiteral("Packet loss"), QStringLiteral("Jitter (ms)")});
    uptimeTable_->setMaximumHeight(150);
    uptimeCardLayout->addWidget(uptimeTable_);
    layout->addWidget(uptimeCard);

    auto* latencyCard = make_card(page);
    auto* latencyCardLayout = new QVBoxLayout(latencyCard);
    latencyCardLayout->setContentsMargins(16, 14, 16, 14);
    auto* latencyTitle = new QLabel(QStringLiteral("Latency"), latencyCard);
    latencyTitle->setFont(cardTitleFont);
    latencyCardLayout->addWidget(latencyTitle);
    latencyTarget_ = new QLabel(latencyCard);
    latencyCardLayout->addWidget(latencyTarget_);
    latencyChart_ = new ChartWidget(QString(), 0.0, 50.0, latencyCard);
    latencyChart_->setMinimumHeight(150);
    latencyCardLayout->addWidget(latencyChart_);
    layout->addWidget(latencyCard);

    auto* speedCard = make_card(page);
    auto* speedCardLayout = new QVBoxLayout(speedCard);
    speedCardLayout->setContentsMargins(16, 14, 16, 14);
    auto* speedTitle = new QLabel(QStringLiteral("Download speed"), speedCard);
    speedTitle->setFont(cardTitleFont);
    speedCardLayout->addWidget(speedTitle);
    speedTestLabel_ = new QLabel(speedCard);
    speedCardLayout->addWidget(speedTestLabel_);
    speedTestChart_ = new ChartWidget(QString(), 0.0, 100.0, speedCard);
    speedTestChart_->setMinimumHeight(120);
    speedCardLayout->addWidget(speedTestChart_);
    layout->addWidget(speedCard);

    auto* outageCard = make_card(page);
    auto* outageCardLayout = new QVBoxLayout(outageCard);
    outageCardLayout->setContentsMargins(16, 14, 16, 14);
    auto* outageTitle = new QLabel(QStringLiteral("Recent outages"), outageCard);
    outageTitle->setFont(cardTitleFont);
    outageCardLayout->addWidget(outageTitle);
    outageTable_ = new QTableWidget(0, 0, outageCard);
    configure_table(outageTable_,
                    {QStringLiteral("Target"), QStringLiteral("Started"), QStringLiteral("Ended"),
                     QStringLiteral("Failed samples")});
    outageCardLayout->addWidget(outageTable_);
    layout->addWidget(outageCard, 1);
    return page;
}

void MainWindow::refreshInternet() {
    if (uptimeTable_ == nullptr) {
        return;
    }
    using nexus::module::connectivity::PathStatus;
    using nexus::module::connectivity::ProbeKind;
    const auto now = nexus::core::now();
    const auto hour_ago = now - std::chrono::hours{1};

    if (internetPathCard_ != nullptr) {
        const auto path = conn_.latest_path_status();
        const PathStatus status = path ? path->status : PathStatus::Unknown;
        QString badge;
        QString detail;
        QColor color;
        switch (status) {
            case PathStatus::AllOk:
                badge = QStringLiteral("Connected");
                detail = QStringLiteral("PC ✓ — Router ✓ — Internet ✓: all reachable");
                color = QColor(theme::kSuccessFg);
                break;
            case PathStatus::LocalIssue:
                badge = QStringLiteral("Router unreachable");
                detail = QStringLiteral("PC ✓ — Router ✗ — Internet ?: can't reach your router");
                color = QColor(theme::kCriticalFg);
                break;
            case PathStatus::BeyondRouter:
                badge = QStringLiteral("Internet unreachable");
                detail = QStringLiteral(
                    "PC ✓ — Router ✓ — Internet ✗: router's fine, nothing beyond it");
                color = QColor(theme::kWarningFg);
                break;
            case PathStatus::NoGatewayFound:
                badge = QStringLiteral("No gateway");
                detail = QStringLiteral("PC ✓ — Router ? — Internet ✗: no network gateway found");
                color = QColor(theme::kCriticalFg);
                break;
            case PathStatus::Unknown:
            default:
                badge = QStringLiteral("Unknown");
                detail = QStringLiteral("PC — Router — Internet: not yet known");
                color = QColor(theme::kNeutralFg);
                break;
        }
        internetPathCard_->setValue(badge);
        internetPathCard_->setValueColor(color);
        internetPathCard_->setSublabel(detail);
    }

    const auto targets = conn_.targets();
    uptimeTable_->setRowCount(static_cast<int>(targets.size()));
    for (int row = 0; row < static_cast<int>(targets.size()); ++row) {
        const auto& target = targets[static_cast<std::size_t>(row)];
        const QString label = target.label.empty() ? QString::fromStdString(target.id)
                                                   : QString::fromStdString(target.label);
        uptimeTable_->setItem(row, 0, new QTableWidgetItem(label));
        const auto uptime = conn_.uptime_fraction(target.id, hour_ago);
        uptimeTable_->setItem(
            row, 1,
            new QTableWidgetItem(uptime ? QStringLiteral("%1%").arg(*uptime * 100.0, 0, 'f', 1)
                                        : QStringLiteral("-")));

        const auto reliability = conn_.reliability_stats(target.id, hour_ago);
        uptimeTable_->setItem(
            row, 2,
            new QTableWidgetItem(reliability.sent > 0
                                     ? QStringLiteral("%1%").arg(
                                           reliability.loss_fraction * 100.0, 0, 'f', 1)
                                     : QStringLiteral("-")));
        uptimeTable_->setItem(
            row, 3,
            new QTableWidgetItem(reliability.received >= 2
                                     ? QStringLiteral("%1").arg(
                                           reliability.jitter.count() / 1000.0, 0, 'f', 1)
                                     : QStringLiteral("-")));
    }

    std::string latency_target;
    for (const auto& target : targets) {
        if (target.kind == ProbeKind::Icmp) {
            latency_target = target.id;
            break;
        }
    }
    QList<QPointF> latency;
    if (!latency_target.empty()) {
        latencyTarget_->setText(
            QStringLiteral("Target: %1").arg(QString::fromStdString(latency_target)));
        for (const auto& sample : conn_.samples_since(latency_target, now - std::chrono::minutes{10})) {
            if (sample.rtt.has_value()) {
                const double ms =
                    std::chrono::duration<double, std::milli>(*sample.rtt).count();
                latency.append(QPointF(seconds_ago(now, sample.at), ms));
            }
        }
    }
    latencyChart_->setPoints(latency, /*autoscaleY=*/true);
    internetLatencyCard_->setValue(latency.isEmpty()
                                       ? QStringLiteral("-")
                                       : QStringLiteral("%1 ms").arg(latency.back().y(), 0, 'f', 0));
    internetLatencyCard_->setSublabel(latency_target.empty()
                                          ? QStringLiteral("no target yet")
                                          : QStringLiteral("Target: %1").arg(
                                                QString::fromStdString(latency_target)));

    // speed_tests existed in the schema from the start but nothing ever
    // populated or read it until SpeedTester (connectivity_module.cpp).
    const auto speed_tests = conn_.recent_speed_tests(50);
    if (speed_tests.empty()) {
        speedTestLabel_->setText(QStringLiteral("Speed test: no runs yet (runs hourly)"));
        internetSpeedCard_->setValue(QStringLiteral("-"));
        internetSpeedCard_->setSublabel(QStringLiteral("no runs yet"));
    } else {
        const auto& latest = speed_tests.front();
        internetSpeedCard_->setValue(latest.download_bps
                                         ? QStringLiteral("%1 Mbps").arg(
                                               *latest.download_bps / 1'000'000.0, 0, 'f', 1)
                                         : QStringLiteral("-"));
        internetSpeedCard_->setSublabel(QStringLiteral("as of %1").arg(format_time(latest.ran_at)));
        speedTestLabel_->setText(
            latest.download_bps
                ? QStringLiteral("Speed test: %1 Mbps as of %2")
                      .arg(*latest.download_bps / 1'000'000.0, 0, 'f', 1)
                      .arg(format_time(latest.ran_at))
                : QStringLiteral("Speed test: last run failed (%1)").arg(format_time(latest.ran_at)));
    }
    QList<QPointF> speed_points;
    for (auto it = speed_tests.rbegin(); it != speed_tests.rend(); ++it) {
        if (it->download_bps) {
            speed_points.append(
                QPointF(seconds_ago(now, it->ran_at), *it->download_bps / 1'000'000.0));
        }
    }
    speedTestChart_->setPoints(speed_points, /*autoscaleY=*/true);

    const auto outages = conn_.recent_outages(20);
    outageTable_->setRowCount(static_cast<int>(outages.size()));
    for (int row = 0; row < static_cast<int>(outages.size()); ++row) {
        const auto& outage = outages[static_cast<std::size_t>(row)];
        outageTable_->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(outage.target_id)));
        outageTable_->setItem(row, 1, new QTableWidgetItem(format_time(outage.started_at)));
        outageTable_->setItem(row, 2,
                              new QTableWidgetItem(outage.ended_at ? format_time(*outage.ended_at)
                                                                  : QStringLiteral("ongoing")));
        outageTable_->setItem(row, 3,
                              new QTableWidgetItem(QString::number(outage.samples_failed)));
    }
}

QWidget* MainWindow::buildReportsPage() {
    auto* page = new QWidget(pages_);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(12);
    layout->addWidget(page_heading(page, QStringLiteral("Reports")));

    auto* generators = new QGroupBox(QStringLiteral("Generate"), page);
    auto* genLayout = new QVBoxLayout(generators);
    for (const auto& info : ctx_.reports.generators()) {
        const QString kind = QString::fromStdString(info.kind);
        auto* row = new QHBoxLayout();
        row->addWidget(new QLabel(QString::fromStdString(info.title), generators));
        auto* html = new QPushButton(QStringLiteral("HTML"), generators);
        auto* csv = new QPushButton(QStringLiteral("CSV"), generators);
        connect(html, &QPushButton::clicked, this, [this, kind] { generateReport(kind, false); });
        connect(csv, &QPushButton::clicked, this, [this, kind] { generateReport(kind, true); });
        row->addStretch(1);
        row->addWidget(html);
        row->addWidget(csv);
        genLayout->addLayout(row);
    }
    if (ctx_.reports.generators().empty()) {
        genLayout->addWidget(new QLabel(QStringLiteral("No report generators registered."),
                                        generators));
    }
    layout->addWidget(generators);

    layout->addWidget(new QLabel(QStringLiteral("Generated reports (double-click to open)"), page));
    reportsTable_ = new QTableWidget(0, 0, page);
    configure_table(reportsTable_, {QStringLiteral("Created"), QStringLiteral("Title"),
                                    QStringLiteral("Format"), QStringLiteral("Path")});
    connect(reportsTable_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        auto* item = reportsTable_->item(row, 3);
        if (item != nullptr) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(item->text()));
        }
    });
    layout->addWidget(reportsTable_, 1);
    return page;
}

void MainWindow::generateReport(const QString& kind, bool csv) {
    try {
        const auto record = ctx_.reports.generate(
            kind.toStdString(),
            csv ? nexus::services::ReportFormat::Csv : nexus::services::ReportFormat::Html);
        ctx_.audit.record("report_generate", kind.toStdString(), record.format, "desktop");
        refreshReports();
        QDesktopServices::openUrl(
            QUrl::fromLocalFile(QString::fromStdString(record.path.string())));
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QStringLiteral("Report failed: %1").arg(QString::fromUtf8(ex.what())),
                                 5000);
    }
}

void MainWindow::refreshReports() {
    if (reportsTable_ == nullptr) {
        return;
    }
    const auto reports = ctx_.reports.recent(50);
    reportsTable_->setRowCount(static_cast<int>(reports.size()));
    for (int row = 0; row < static_cast<int>(reports.size()); ++row) {
        const auto& report = reports[static_cast<std::size_t>(row)];
        reportsTable_->setItem(row, 0, new QTableWidgetItem(format_time(report.created_at)));
        reportsTable_->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(report.title)));
        reportsTable_->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(report.format)));
        reportsTable_->setItem(row, 3,
                               new QTableWidgetItem(QString::fromStdString(report.path.string())));
    }
}

bool MainWindow::confirmHeavyJob(const QString& label) {
    const auto active = ctx_.heavy_jobs.active();
    if (active.empty()) {
        return true;
    }
    QStringList running;
    for (const auto& a : active) {
        running << qstr(a);
    }
    const auto answer = QMessageBox::question(
        this, QStringLiteral("Another heavy job is running"),
        QStringLiteral("Already running: %1.\n\nStart %2 anyway? Running several "
                       "disk-intensive jobs at once can slow all of them down (UFR-018).")
            .arg(running.join(QStringLiteral(", ")), label));
    return answer == QMessageBox::Yes;
}

nexus::jobs::Throttle MainWindow::currentThrottle() const {
    const std::string raw = ctx_.settings.get_or(
        "throttle.level", std::string(nexus::jobs::to_string(nexus::jobs::ThrottleLevel::Unlimited)));
    return nexus::jobs::Throttle(
        nexus::jobs::throttle_level_from_string(raw).value_or(nexus::jobs::ThrottleLevel::Unlimited));
}

QWidget* MainWindow::buildStoragePage() {
    auto* page = new QWidget(pages_);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(12);
    layout->addWidget(page_heading(page, QStringLiteral("Storage")));

    QFont cardTitleFont = page->font();
    cardTitleFont.setBold(true);

    auto* statsRow = new QHBoxLayout();
    statsRow->setSpacing(16);
    storageFreeCard_ = new StatCard(theme::load_nav_icon(QStringLiteral("storage.svg")),
                                    QStringLiteral("Storage Free"), page);
    storageFreeCard_->setProgressColor(QColor(theme::kAction));
    statsRow->addWidget(storageFreeCard_);
    storageReclaimableCard_ = new StatCard(theme::load_nav_icon(QStringLiteral("storage.svg")),
                                           QStringLiteral("Reclaimable"), page);
    storageReclaimableCard_->setProgress(-1);
    statsRow->addWidget(storageReclaimableCard_);
    storageDuplicatesCard_ = new StatCard(theme::load_nav_icon(QStringLiteral("storage.svg")),
                                          QStringLiteral("Duplicate Groups"), page);
    storageDuplicatesCard_->setProgress(-1);
    statsRow->addWidget(storageDuplicatesCard_);
    layout->addLayout(statsRow);

    auto* folderRow = new QHBoxLayout();
    storageFolder_ = new QLineEdit(page);
    storageFolder_->setReadOnly(true);
    storageFolder_->setPlaceholderText(QStringLiteral("Choose a folder to scan for duplicates"));
    auto* choose = new QPushButton(QStringLiteral("Choose…"), page);
    connect(choose, &QPushButton::clicked, this, &MainWindow::chooseStorageFolder);
    storageScanButton_ = new QPushButton(QStringLiteral("Scan"), page);
    storageScanButton_->setEnabled(false);
    connect(storageScanButton_, &QPushButton::clicked, this, &MainWindow::startStorageScan);
    folderRow->addWidget(storageFolder_, 1);
    folderRow->addWidget(choose);
    folderRow->addWidget(storageScanButton_);
    layout->addLayout(folderRow);

    storageWatchToggle_ = new QCheckBox(
        QStringLiteral("Auto-rescan this folder when files change"), page);
    connect(storageWatchToggle_, &QCheckBox::toggled, this, &MainWindow::toggleStorageWatch);
    layout->addWidget(storageWatchToggle_);

    storageProgress_ = new QProgressBar(page);
    storageProgress_->setRange(0, 100);
    storageProgress_->hide();
    storagePhase_ = new QLabel(page);
    storagePhase_->hide();
    layout->addWidget(storageProgress_);
    layout->addWidget(storagePhase_);

    storageSummary_ = new QLabel(page);
    layout->addWidget(storageSummary_);

    storageUsageBar_ = new QProgressBar(page);
    storageUsageBar_->setRange(0, 100);
    storageUsageBar_->setTextVisible(true);
    storageUsageBar_->setFormat(QStringLiteral("%p% of scanned data is reclaimable duplicates"));
    storageUsageBar_->hide();
    layout->addWidget(storageUsageBar_);

    auto* treeCard = make_card(page);
    auto* treeCardLayout = new QVBoxLayout(treeCard);
    treeCardLayout->setContentsMargins(16, 14, 16, 14);
    auto* treeTitle = new QLabel(QStringLiteral("Duplicate files"), treeCard);
    treeTitle->setFont(cardTitleFont);
    treeCardLayout->addWidget(treeTitle);
    storageTree_ = new QTreeWidget(treeCard);
    storageTree_->setColumnCount(2);
    storageTree_->setHeaderLabels({QStringLiteral("File"), QStringLiteral("Size")});
    storageTree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    treeCardLayout->addWidget(storageTree_);
    storageRecycleButton_ = new QPushButton(QStringLiteral("Move checked to Recycle Bin"), treeCard);
    storageRecycleButton_->setEnabled(false);
    connect(storageRecycleButton_, &QPushButton::clicked, this,
            &MainWindow::recycleCheckedDuplicates);
    treeCardLayout->addWidget(storageRecycleButton_);
    layout->addWidget(treeCard, 1);

    auto* historyCard = make_card(page);
    auto* historyCardLayout = new QVBoxLayout(historyCard);
    historyCardLayout->setContentsMargins(16, 14, 16, 14);
    auto* historyTitle = new QLabel(QStringLiteral("Scan history"), historyCard);
    historyTitle->setFont(cardTitleFont);
    historyCardLayout->addWidget(historyTitle);
    storageHistoryTable_ = new QTableWidget(0, 0, historyCard);
    configure_table(storageHistoryTable_,
                    {QStringLiteral("Started"), QStringLiteral("Folder"), QStringLiteral("State"),
                     QStringLiteral("Files"), QStringLiteral("Duplicate groups"),
                     QStringLiteral("Reclaimable")});
    storageHistoryTable_->setMaximumHeight(160);
    historyCardLayout->addWidget(storageHistoryTable_);
    layout->addWidget(historyCard);

    refreshStorageSummary();
    return page;
}

void MainWindow::refreshStorageSummary() {
    if (storageSummary_ == nullptr) {
        return;
    }
    const auto scan = storage_.latest_scan();
    if (!scan) {
        storageSummary_->setText(QStringLiteral("No scans yet."));
        storageReclaimableCard_->setValue(QStringLiteral("-"));
        storageReclaimableCard_->setSublabel(QStringLiteral("no scans yet"));
        storageDuplicatesCard_->setValue(QStringLiteral("-"));
        storageDuplicatesCard_->setSublabel(QStringLiteral("no scans yet"));
    } else {
        storageSummary_->setText(
            QStringLiteral("Last scan of %1 - %2 duplicate group(s), %3 reclaimable")
                .arg(QString::fromStdString(scan->root))
                .arg(scan->duplicate_groups)
                .arg(human_bytes(scan->reclaimable_bytes)));
        storageReclaimableCard_->setValue(human_bytes(scan->reclaimable_bytes));
        storageReclaimableCard_->setSublabel(
            QStringLiteral("scan of %1").arg(QString::fromStdString(scan->root)));
        storageDuplicatesCard_->setValue(QString::number(scan->duplicate_groups));
        storageDuplicatesCard_->setSublabel(QStringLiteral("groups found"));
    }

    // Storage Free mirrors the Home page's card: same HardwareRepository
    // disk metrics, same "first mount reported" choice.
    const auto snapshot = hw_.latest_snapshot();
    std::string diskMount;
    for (const auto& sample : snapshot) {
        if (sample.metric == "disk.total_bytes") {
            diskMount = sample.scope;
            break;
        }
    }
    std::optional<double> diskFreeFraction;
    std::optional<double> diskFreeBytes;
    std::optional<double> diskTotalBytes;
    for (const auto& sample : snapshot) {
        if (sample.scope != diskMount) {
            continue;
        }
        if (sample.metric == "disk.free_fraction") {
            diskFreeFraction = sample.value;
        } else if (sample.metric == "disk.free_bytes") {
            diskFreeBytes = sample.value;
        } else if (sample.metric == "disk.total_bytes") {
            diskTotalBytes = sample.value;
        }
    }
    if (diskFreeFraction && diskFreeBytes && diskTotalBytes) {
        storageFreeCard_->setValue(QStringLiteral("%1%").arg(*diskFreeFraction * 100.0, 0, 'f', 0));
        storageFreeCard_->setProgress(static_cast<int>(*diskFreeFraction * 100.0));
        storageFreeCard_->setSublabel(
            QStringLiteral("%1 free of %2")
                .arg(human_bytes(static_cast<std::uint64_t>(*diskFreeBytes)),
                     human_bytes(static_cast<std::uint64_t>(*diskTotalBytes))));
    } else {
        storageFreeCard_->setValue(QStringLiteral("-"));
        storageFreeCard_->setProgress(0);
        storageFreeCard_->setSublabel(QStringLiteral("no samples yet"));
    }

    // Previously the page only ever showed the latest scan - the history
    // itself was already queryable (StorageRepository::scans()) but unused
    // by the UI.
    const auto history = storage_.scans(20);
    storageHistoryTable_->setRowCount(static_cast<int>(history.size()));
    for (int row = 0; row < static_cast<int>(history.size()); ++row) {
        const auto& record = history[static_cast<std::size_t>(row)];
        storageHistoryTable_->setItem(row, 0, new QTableWidgetItem(format_time(record.started_at)));
        storageHistoryTable_->setItem(row, 1,
                                      new QTableWidgetItem(QString::fromStdString(record.root)));
        storageHistoryTable_->setItem(row, 2,
                                      new QTableWidgetItem(QString::fromStdString(record.state)));
        storageHistoryTable_->setItem(row, 3, new QTableWidgetItem(QString::number(record.files_seen)));
        storageHistoryTable_->setItem(row, 4,
                                      new QTableWidgetItem(QString::number(record.duplicate_groups)));
        storageHistoryTable_->setItem(row, 5,
                                      new QTableWidgetItem(human_bytes(record.reclaimable_bytes)));
    }
}

void MainWindow::chooseStorageFolder() {
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Choose a folder"));
    if (!dir.isEmpty()) {
        // A watcher armed for the previous folder would be watching the
        // wrong tree now - stop it; it's re-armed (if the toggle is still
        // checked) once the new folder's first scan completes.
        storageWatcher_.reset();
        storageFolder_->setText(dir);
        storageScanButton_->setEnabled(!storageScanning_);
    }
}

void MainWindow::toggleStorageWatch(bool enabled) {
    if (!enabled) {
        storageWatcher_.reset();
        return;
    }
    if (!storageScanning_ && !storageFolder_->text().isEmpty()) {
        startWatchingStorageFolder(storageFolder_->text().toStdWString());
    }
    // If a scan is in flight or no folder is chosen yet, applyScanResults()
    // arms the watch itself once a scan of the current folder completes.
}

void MainWindow::startWatchingStorageFolder(const std::filesystem::path& root) {
    if (storageWatcher_ != nullptr && storageWatcherRoot_ == root) {
        return; // already watching this exact folder - leave it running
    }
    storageWatcher_.reset();
    storageWatcherRoot_ = root;

    const QPointer<MainWindow> self(this);
    storageWatcher_ = std::make_unique<nexus::fs::DirectoryWatcher>(
        root, [self](const std::vector<nexus::fs::FileChange>&) {
            QMetaObject::invokeMethod(
                qApp,
                [self] {
                    if (self && self->storageWatchToggle_ != nullptr &&
                        self->storageWatchToggle_->isChecked() && !self->storageScanning_) {
                        self->startStorageScan();
                    }
                },
                Qt::QueuedConnection);
        });
    if (!storageWatcher_->start()) {
        storageWatcher_.reset();
        statusBar()->showMessage(
            QStringLiteral("Couldn't watch this folder for changes - auto-rescan is off"), 5000);
    }
}

void MainWindow::startStorageScan() {
    if (storageScanning_ || storageFolder_->text().isEmpty()) {
        return;
    }
    if (!confirmHeavyJob(QStringLiteral("Storage scan"))) {
        return;
    }
    storageScanning_ = true;
    storageScanButton_->setEnabled(false);
    storageRecycleButton_->setEnabled(false);
    storageTree_->clear();
    storageProgress_->setValue(0);
    storageProgress_->show();
    storagePhase_->show();
    storagePhase_->setText(QStringLiteral("starting…"));
    storageCancel_ = std::make_shared<std::atomic<bool>>(false);

    const std::filesystem::path root = storageFolder_->text().toStdWString();
    const auto cancel = storageCancel_;
    const QPointer<MainWindow> self(this);
    auto* db = &ctx_.db;
    auto heavy_lease = std::make_shared<nexus::services::HeavyJobGuard::Lease>(
        ctx_.heavy_jobs.acquire("Storage scan"));
    const nexus::jobs::Throttle throttle = currentThrottle();

    ctx_.pool.submit([self, root, cancel, db, heavy_lease, throttle] {
        nexus::module::storage::StorageRepository repo(*db);
        nexus::module::storage::DuplicateScanner scanner(&repo);

        auto summary = scanner.scan(
            root, nexus::fs::ExclusionRules::defaults(), {},
            [self](double fraction, std::string_view phase) {
                const int percent = static_cast<int>(fraction * 100.0);
                const QString label =
                    QString::fromUtf8(phase.data(), static_cast<qsizetype>(phase.size()));
                QMetaObject::invokeMethod(
                    qApp,
                    [self, percent, label] {
                        if (self && self->storageProgress_ != nullptr) {
                            self->storageProgress_->setValue(percent);
                            self->storagePhase_->setText(label);
                        }
                    },
                    Qt::QueuedConnection);
            },
            [cancel] { return cancel->load(); }, [throttle] { throttle.pace(); });

        QMetaObject::invokeMethod(
            qApp,
            [self, result = std::move(summary)] {
                if (self) {
                    self->applyScanResults(result);
                }
            },
            Qt::QueuedConnection);
    });
}

void MainWindow::applyScanResults(const nexus::module::storage::ScanSummary& summary) {
    storageScanning_ = false;
    storageProgress_->hide();
    storagePhase_->hide();
    storageScanButton_->setEnabled(!storageFolder_->text().isEmpty());

    // Scan history and the Reclaimable/Duplicate Groups stat cards were
    // otherwise only ever populated once, at page construction - never
    // updated after a scan actually completed. Refreshed first so the
    // scan-specific summary text set below (which knows about cancellation)
    // is the one left on screen, not this call's more generic wording.
    refreshStorageSummary();

    storageTree_->clear();
    for (const auto& group : summary.groups) {
        auto* top = new QTreeWidgetItem(storageTree_);
        top->setFirstColumnSpanned(true);
        top->setText(0, QStringLiteral("%1 files · %2 each · %3 reclaimable")
                            .arg(group.files.size())
                            .arg(human_bytes(group.file_size))
                            .arg(human_bytes(group.reclaimable_bytes())));
        bool keep = true;
        for (const auto& file : group.files) {
            auto* child = new QTreeWidgetItem(top);
            child->setText(0, QString::fromStdString(file.generic_string()));
            child->setText(1, human_bytes(group.file_size));
            child->setFlags(child->flags() | Qt::ItemIsUserCheckable);
            // Keep the first file in each group; pre-check the rest for removal.
            child->setCheckState(0, keep ? Qt::Unchecked : Qt::Checked);
            keep = false;
        }
        top->setExpanded(true);
    }

    storageRecycleButton_->setEnabled(!summary.groups.empty());
    storageSummary_->setText(
        QStringLiteral("%1 duplicate group(s), %2 reclaimable%3")
            .arg(summary.groups.size())
            .arg(human_bytes(summary.reclaimable_bytes()))
            .arg(summary.cancelled ? QStringLiteral(" - scan cancelled") : QString()));

    if (summary.bytes_seen > 0) {
        const int percent = static_cast<int>(
            (static_cast<double>(summary.reclaimable_bytes()) / static_cast<double>(summary.bytes_seen)) *
            100.0);
        storageUsageBar_->setValue(std::clamp(percent, 0, 100));
        storageUsageBar_->show();
    } else {
        storageUsageBar_->hide();
    }

    if (summary.reclaimable_bytes() > 0) {
        ctx_.events.publish(nexus::services::events::DuplicatesFoundEvent{
            summary.reclaimable_bytes(), storageFolder_->text().toStdString()});
    }

    if (!summary.cancelled && storageWatchToggle_ != nullptr && storageWatchToggle_->isChecked() &&
        !storageFolder_->text().isEmpty()) {
        startWatchingStorageFolder(storageFolder_->text().toStdWString());
    }
}

void MainWindow::recycleCheckedDuplicates() {
    std::vector<std::filesystem::path> targets;
    for (int i = 0; i < storageTree_->topLevelItemCount(); ++i) {
        const QTreeWidgetItem* top = storageTree_->topLevelItem(i);
        for (int j = 0; j < top->childCount(); ++j) {
            const QTreeWidgetItem* child = top->child(j);
            if (child->checkState(0) == Qt::Checked) {
                targets.emplace_back(child->text(0).toStdWString());
            }
        }
    }
    if (targets.empty()) {
        return;
    }

    const auto answer = QMessageBox::question(
        this, QStringLiteral("Move to Recycle Bin"),
        QStringLiteral("Move %1 file(s) to the Recycle Bin?").arg(targets.size()));
    if (answer != QMessageBox::Yes) {
        return;
    }

    storageRecycleButton_->setEnabled(false);
    const QPointer<MainWindow> self(this);
    auto* audit = &ctx_.audit;
    auto* notifications = &ctx_.notifications;

    ctx_.pool.submit([self, targets, audit, notifications] {
        const auto result = nexus::module::storage::recycle_to_bin(targets);
        audit->record("recycle_duplicates", {},
                      std::to_string(result.recycled) + " recycled, " +
                          std::to_string(result.failed.size()) + " failed",
                      "desktop");
        notifications->post(
            "storage",
            result.ok() ? nexus::notify::Severity::Success : nexus::notify::Severity::Warning,
            "Recycled " + std::to_string(result.recycled) + " file(s)", result.error);
        QMetaObject::invokeMethod(
            qApp,
            [self] {
                if (self) {
                    self->startStorageScan();
                }
            },
            Qt::QueuedConnection);
    });
}

// ----- Vault ----------------------------------------------------------------

QWidget* MainWindow::buildVaultPage() {
    auto* page = new QWidget(pages_);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(12);
    layout->addWidget(page_heading(page, QStringLiteral("Vault")));

    auto* note = new QLabel(
        QStringLiteral("Your vault runs in its own process (nexuspc-vault), isolated from the "
                       "rest of NexusPC - no other module can read a decrypted entry."),
        page);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: palette(mid);"));
    layout->addWidget(note);

    vaultStatus_ = new QLabel(QStringLiteral("Not connected."), page);
    layout->addWidget(vaultStatus_);

    // ---- Locked / no-vault-yet panel ----
    vaultLockedPanel_ = new QWidget(page);
    auto* lockedLayout = new QVBoxLayout(vaultLockedPanel_);
    lockedLayout->setContentsMargins(0, 0, 0, 0);

    vaultPasswordInput_ = new QLineEdit(vaultLockedPanel_);
    vaultPasswordInput_->setObjectName(QStringLiteral("vaultPasswordInput"));
    vaultPasswordInput_->setEchoMode(QLineEdit::Password);
    vaultPasswordInput_->setPlaceholderText(QStringLiteral("Master password"));
    connect(vaultPasswordInput_, &QLineEdit::returnPressed, this, &MainWindow::vaultUnlockOrCreate);
    lockedLayout->addWidget(vaultPasswordInput_);

    auto* confirmRow = new QHBoxLayout();
    vaultConfirmLabel_ = new QLabel(QStringLiteral("Confirm:"), vaultLockedPanel_);
    vaultPasswordConfirm_ = new QLineEdit(vaultLockedPanel_);
    vaultPasswordConfirm_->setObjectName(QStringLiteral("vaultPasswordConfirm"));
    vaultPasswordConfirm_->setEchoMode(QLineEdit::Password);
    connect(vaultPasswordConfirm_, &QLineEdit::returnPressed, this, &MainWindow::vaultUnlockOrCreate);
    confirmRow->addWidget(vaultConfirmLabel_);
    confirmRow->addWidget(vaultPasswordConfirm_, 1);
    lockedLayout->addLayout(confirmRow);
    vaultConfirmLabel_->hide();
    vaultPasswordConfirm_->hide();

    vaultUnlockButton_ = new QPushButton(QStringLiteral("Unlock"), vaultLockedPanel_);
    vaultUnlockButton_->setObjectName(QStringLiteral("vaultUnlockButton"));
    connect(vaultUnlockButton_, &QPushButton::clicked, this, &MainWindow::vaultUnlockOrCreate);
    lockedLayout->addWidget(vaultUnlockButton_);
    lockedLayout->addStretch(1);
    layout->addWidget(vaultLockedPanel_);

    // ---- Unlocked panel ----
    vaultUnlockedPanel_ = new QWidget(page);
    auto* unlockedLayout = new QVBoxLayout(vaultUnlockedPanel_);
    unlockedLayout->setContentsMargins(0, 0, 0, 0);

    auto* toolbar = new QHBoxLayout();
    auto* newButton = new QPushButton(QStringLiteral("New entry"), vaultUnlockedPanel_);
    connect(newButton, &QPushButton::clicked, this, &MainWindow::newVaultEntry);
    auto* healthButton = new QPushButton(QStringLiteral("Health check"), vaultUnlockedPanel_);
    connect(healthButton, &QPushButton::clicked, this, &MainWindow::showVaultHealth);
    auto* exportButton = new QPushButton(QStringLiteral("Export…"), vaultUnlockedPanel_);
    connect(exportButton, &QPushButton::clicked, this, &MainWindow::exportVault);
    auto* lockButton = new QPushButton(QStringLiteral("Lock now"), vaultUnlockedPanel_);
    connect(lockButton, &QPushButton::clicked, this, &MainWindow::vaultLockNow);
    toolbar->addWidget(newButton);
    toolbar->addWidget(healthButton);
    toolbar->addWidget(exportButton);
    toolbar->addStretch(1);
    toolbar->addWidget(lockButton);
    unlockedLayout->addLayout(toolbar);

    auto* splitter = new QSplitter(vaultUnlockedPanel_);
    vaultEntryList_ = new QListWidget(splitter);
    vaultEntryList_->setObjectName(QStringLiteral("vaultEntryList"));
    vaultEntryList_->setMaximumWidth(260);
    connect(vaultEntryList_, &QListWidget::currentRowChanged, this,
           [this](int) { vaultSelectionChanged(); });

    auto* detail = new QWidget(splitter);
    auto* form = new QFormLayout(detail);
    vaultForm_ = form;
    vaultEntryKind_ = new QComboBox(detail);
    vaultEntryKind_->addItem(QStringLiteral("Password"));
    vaultEntryKind_->addItem(QStringLiteral("Secure note"));
    connect(vaultEntryKind_, &QComboBox::currentIndexChanged, this,
           &MainWindow::vaultEntryKindChanged);
    vaultEntryTitle_ = new QLineEdit(detail);
    vaultEntryTitle_->setObjectName(QStringLiteral("vaultEntryTitle"));
    vaultEntryUsername_ = new QLineEdit(detail);
    vaultEntryUsername_->setObjectName(QStringLiteral("vaultEntryUsername"));

    auto* passwordRow = new QHBoxLayout();
    vaultPasswordRow_ = passwordRow;
    vaultEntryPassword_ = new QLineEdit(detail);
    vaultEntryPassword_->setObjectName(QStringLiteral("vaultEntryPassword"));
    vaultEntryPassword_->setEchoMode(QLineEdit::Password);
    auto* toggleVisibility = new QPushButton(QStringLiteral("Show"), detail);
    toggleVisibility->setCheckable(true);
    connect(toggleVisibility, &QPushButton::toggled, this, [this, toggleVisibility](bool checked) {
        vaultEntryPassword_->setEchoMode(checked ? QLineEdit::Normal : QLineEdit::Password);
        toggleVisibility->setText(checked ? QStringLiteral("Hide") : QStringLiteral("Show"));
    });
    auto* generateButton = new QPushButton(QStringLiteral("Generate"), detail);
    connect(generateButton, &QPushButton::clicked, this, &MainWindow::generateVaultPassword);
    auto* copyButton = new QPushButton(QStringLiteral("Copy"), detail);
    connect(copyButton, &QPushButton::clicked, this, &MainWindow::copyVaultPassword);
    passwordRow->addWidget(vaultEntryPassword_, 1);
    passwordRow->addWidget(toggleVisibility);
    passwordRow->addWidget(generateButton);
    passwordRow->addWidget(copyButton);

    vaultEntryUrl_ = new QLineEdit(detail);
    vaultEntryTags_ = new QLineEdit(detail);
    vaultEntryTags_->setPlaceholderText(QStringLiteral("comma, separated, tags"));
    vaultEntryNotes_ = new QPlainTextEdit(detail);
    vaultEntryNotes_->setFixedHeight(100);

    form->addRow(QStringLiteral("Kind"), vaultEntryKind_);
    form->addRow(QStringLiteral("Title"), vaultEntryTitle_);
    form->addRow(QStringLiteral("Username"), vaultEntryUsername_);
    form->addRow(QStringLiteral("Password"), passwordRow);
    form->addRow(QStringLiteral("URL"), vaultEntryUrl_);
    form->addRow(QStringLiteral("Tags"), vaultEntryTags_);
    form->addRow(QStringLiteral("Notes"), vaultEntryNotes_);

    auto* buttonsRow = new QHBoxLayout();
    vaultSaveButton_ = new QPushButton(QStringLiteral("Save"), detail);
    vaultSaveButton_->setObjectName(QStringLiteral("vaultSaveButton"));
    connect(vaultSaveButton_, &QPushButton::clicked, this, &MainWindow::saveVaultEntry);
    vaultDeleteButton_ = new QPushButton(QStringLiteral("Delete"), detail);
    vaultDeleteButton_->setObjectName(QStringLiteral("vaultDeleteButton"));
    vaultDeleteButton_->setEnabled(false);
    connect(vaultDeleteButton_, &QPushButton::clicked, this, &MainWindow::deleteVaultEntry);
    buttonsRow->addWidget(vaultSaveButton_);
    buttonsRow->addWidget(vaultDeleteButton_);
    buttonsRow->addStretch(1);
    form->addRow(buttonsRow);

    splitter->addWidget(vaultEntryList_);
    splitter->addWidget(detail);
    splitter->setStretchFactor(1, 1);
    unlockedLayout->addWidget(splitter, 1);

    layout->addWidget(vaultUnlockedPanel_, 1);
    vaultUnlockedPanel_->hide();

    vaultClipboardTimer_ = new QTimer(this);
    vaultClipboardTimer_->setSingleShot(true);
    connect(vaultClipboardTimer_, &QTimer::timeout, this, &MainWindow::clearVaultClipboardIfUnchanged);

    return page;
}

void MainWindow::vaultRequestAsync(nlohmann::json body, std::function<void(nlohmann::json)> onDone) {
    const QPointer<MainWindow> self(this);
    auto* vault = &vault_;
    ctx_.pool.submit([self, vault, body = std::move(body), onDone = std::move(onDone)] {
        auto response = vault->request(body);
        QMetaObject::invokeMethod(
            qApp,
            [self, response = std::move(response), onDone = std::move(onDone)]() mutable {
                if (self) {
                    onDone(std::move(response));
                }
            },
            Qt::QueuedConnection);
    });
}

void MainWindow::refreshVaultStatus() {
    if (vaultStatus_ != nullptr) {
        vaultStatus_->setText(QStringLiteral("Connecting…"));
    }
    vaultRequestAsync({{"verb", "status"}},
                      [this](nlohmann::json response) { applyVaultStatus(response); });
}

void MainWindow::applyVaultStatus(const nlohmann::json& status) {
    if (vaultLockedPanel_ == nullptr) {
        return;
    }

    if (!status.value("ok", false)) {
        vaultStatus_->setText(QStringLiteral("Could not reach the vault: %1")
                                  .arg(qstr(status.value("error", std::string{"unknown error"}))));
        vaultLockedPanel_->show();
        vaultUnlockedPanel_->hide();
        vaultCreateMode_ = false;
        vaultConfirmLabel_->hide();
        vaultPasswordConfirm_->hide();
        vaultUnlockButton_->setText(QStringLiteral("Retry"));
        return;
    }

    const bool locked = status.value("locked", true);
    const bool exists = status.value("vault_exists", false);

    if (!locked) {
        const int count = status.value("entry_count", 0);
        vaultStatus_->setText(QStringLiteral("Unlocked - %1 entr%2")
                                  .arg(count)
                                  .arg(count == 1 ? QStringLiteral("y") : QStringLiteral("ies")));
        vaultLockedPanel_->hide();
        vaultUnlockedPanel_->show();
        refreshVaultEntryList();
        return;
    }

    vaultCreateMode_ = !exists;
    vaultLockedPanel_->show();
    vaultUnlockedPanel_->hide();
    vaultPasswordInput_->clear();
    vaultPasswordConfirm_->clear();
    if (vaultCreateMode_) {
        vaultStatus_->setText(
            QStringLiteral("No vault yet - choose a master password to create one."));
        vaultConfirmLabel_->show();
        vaultPasswordConfirm_->show();
        vaultUnlockButton_->setText(QStringLiteral("Create vault"));
    } else {
        vaultStatus_->setText(QStringLiteral("Locked."));
        vaultConfirmLabel_->hide();
        vaultPasswordConfirm_->hide();
        vaultUnlockButton_->setText(QStringLiteral("Unlock"));
    }
}

void MainWindow::vaultUnlockOrCreate() {
    if (vaultBusy_) {
        return;
    }
    const QString password = vaultPasswordInput_->text();
    if (password.isEmpty()) {
        return;
    }

    if (vaultCreateMode_) {
        if (password != vaultPasswordConfirm_->text()) {
            QMessageBox::warning(this, QStringLiteral("Passwords don't match"),
                                 QStringLiteral("Re-enter the same master password in both fields."));
            return;
        }
        if (password.size() < 8) {
            QMessageBox::warning(
                this, QStringLiteral("Weak master password"),
                QStringLiteral("Use at least 8 characters for the vault's master password."));
            return;
        }
    }

    vaultBusy_ = true;
    vaultUnlockButton_->setEnabled(false);
    vaultStatus_->setText(vaultCreateMode_ ? QStringLiteral("Creating vault…")
                                           : QStringLiteral("Unlocking…"));

    const std::string verb = vaultCreateMode_ ? "create" : "unlock";
    vaultRequestAsync({{"verb", verb}, {"master_password", password.toStdString()}},
                      [this](nlohmann::json response) {
                          vaultBusy_ = false;
                          if (vaultUnlockButton_ != nullptr) {
                              vaultUnlockButton_->setEnabled(true);
                          }
                          if (!response.value("ok", false)) {
                              vaultStatus_->setText(
                                  qstr(response.value("error", std::string{"failed"})));
                              return;
                          }
                          vaultPasswordInput_->clear();
                          vaultPasswordConfirm_->clear();
                          refreshVaultStatus();
                      });
}

void MainWindow::vaultLockNow() {
    vaultRequestAsync({{"verb", "lock"}}, [this](nlohmann::json /*response*/) {
        vaultSelectedEntryId_.clear();
        refreshVaultStatus();
    });
}

void MainWindow::refreshVaultEntryList() {
    vaultRequestAsync({{"verb", "list"}}, [this](nlohmann::json response) {
        if (vaultEntryList_ == nullptr) {
            return;
        }
        vaultEntryList_->clear();
        if (!response.value("ok", false)) {
            return;
        }
        for (const auto& e : response.value("entries", nlohmann::json::array())) {
            const QString title = qstr(e.value("title", std::string{}));
            const QString username = qstr(e.value("username", std::string{}));
            const bool isNote = e.value("kind", std::string{}) == "secure_note";
            QString label = isNote ? QStringLiteral("[Note] ") + title
                                   : (username.isEmpty() ? title
                                                          : title + QStringLiteral(" — ") + username);
            auto* item = new QListWidgetItem(label);
            item->setData(Qt::UserRole, qstr(e.value("id", std::string{})));
            vaultEntryList_->addItem(item);
        }
    });
}

void MainWindow::vaultSelectionChanged() {
    if (vaultEntryList_ == nullptr) {
        return;
    }
    QListWidgetItem* item = vaultEntryList_->currentItem();
    if (item == nullptr) {
        return;
    }
    loadVaultEntry(item->data(Qt::UserRole).toString());
}

void MainWindow::loadVaultEntry(const QString& id) {
    vaultRequestAsync({{"verb", "get"}, {"id", id.toStdString()}},
                      [this, id](nlohmann::json response) {
                          if (!response.value("ok", false) || vaultEntryTitle_ == nullptr) {
                              return;
                          }
                          vaultSelectedEntryId_ = id;
                          const auto& entry = response["entry"];
                          vaultEntryKind_->setCurrentIndex(
                              entry.value("kind", std::string{}) == "secure_note" ? 1 : 0);
                          vaultEntryTitle_->setText(qstr(entry.value("title", std::string{})));
                          vaultEntryUsername_->setText(qstr(entry.value("username", std::string{})));
                          vaultEntryPassword_->setText(qstr(entry.value("password", std::string{})));
                          vaultEntryUrl_->setText(qstr(entry.value("url", std::string{})));
                          vaultEntryNotes_->setPlainText(qstr(entry.value("notes", std::string{})));
                          QStringList tags;
                          for (const auto& t : entry.value("tags", nlohmann::json::array())) {
                              tags << qstr(t.get<std::string>());
                          }
                          vaultEntryTags_->setText(tags.join(QStringLiteral(", ")));
                          vaultDeleteButton_->setEnabled(true);
                      });
}

void MainWindow::newVaultEntry() {
    vaultSelectedEntryId_.clear();
    if (vaultEntryTitle_ == nullptr) {
        return;
    }
    vaultEntryKind_->setCurrentIndex(0);
    vaultEntryTitle_->clear();
    vaultEntryUsername_->clear();
    vaultEntryPassword_->clear();
    vaultEntryUrl_->clear();
    vaultEntryTags_->clear();
    vaultEntryNotes_->clear();
    vaultDeleteButton_->setEnabled(false);
    vaultEntryList_->clearSelection();
    vaultEntryTitle_->setFocus();
}

void MainWindow::vaultEntryKindChanged(int index) {
    if (vaultForm_ == nullptr) {
        return;
    }
    const bool isNote = index == 1;
    // Username/password/URL are meaningless for a secure note - hide the
    // whole row (label included) rather than just clearing/disabling the
    // field, so the form reads as "this is a note," not "a password entry
    // with some fields grayed out."
    vaultForm_->setRowVisible(vaultEntryUsername_, !isNote);
    vaultForm_->setRowVisible(vaultPasswordRow_, !isNote);
    vaultForm_->setRowVisible(vaultEntryUrl_, !isNote);
}

void MainWindow::exportVault() {
    const QString destination = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export vault to"), QStringLiteral("vault-backup.nxv"),
        QStringLiteral("NexusPC Vault (*.nxv)"));
    if (destination.isEmpty()) {
        return;
    }
    vaultRequestAsync(
        {{"verb", "export"}, {"destination", destination.toStdString()}},
        [this, destination](nlohmann::json response) {
            if (!response.value("ok", false)) {
                QMessageBox::warning(this, QStringLiteral("Export failed"),
                                     qstr(response.value("error", std::string{"unknown error"})));
                return;
            }
            statusBar()->showMessage(
                QStringLiteral("Vault exported to %1").arg(destination), 5000);
        });
}

void MainWindow::saveVaultEntry() {
    if (vaultEntryTitle_->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Title required"),
                             QStringLiteral("Give this entry a title."));
        return;
    }

    nlohmann::json tags = nlohmann::json::array();
    for (const QString& t : vaultEntryTags_->text().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        tags.push_back(t.trimmed().toStdString());
    }

    const bool isNote = vaultEntryKind_->currentIndex() == 1;
    const nlohmann::json entry = {
        {"id", vaultSelectedEntryId_.toStdString()},
        {"kind", std::string(isNote ? "secure_note" : "password")},
        {"title", vaultEntryTitle_->text().toStdString()},
        {"username", isNote ? std::string{} : vaultEntryUsername_->text().toStdString()},
        {"password", isNote ? std::string{} : vaultEntryPassword_->text().toStdString()},
        {"url", isNote ? std::string{} : vaultEntryUrl_->text().toStdString()},
        {"notes", vaultEntryNotes_->toPlainText().toStdString()},
        {"tags", tags},
    };

    vaultSaveButton_->setEnabled(false);
    vaultRequestAsync({{"verb", "put"}, {"entry", entry}}, [this](nlohmann::json response) {
        if (vaultSaveButton_ != nullptr) {
            vaultSaveButton_->setEnabled(true);
        }
        if (!response.value("ok", false)) {
            QMessageBox::warning(this, QStringLiteral("Save failed"),
                                 qstr(response.value("error", std::string{"unknown error"})));
            return;
        }
        vaultSelectedEntryId_ = qstr(response.value("id", std::string{}));
        vaultDeleteButton_->setEnabled(true);
        refreshVaultEntryList();
    });
}

void MainWindow::deleteVaultEntry() {
    if (vaultSelectedEntryId_.isEmpty()) {
        return;
    }
    const auto answer = QMessageBox::question(
        this, QStringLiteral("Delete entry"),
        QStringLiteral("Delete \"%1\"? This cannot be undone.").arg(vaultEntryTitle_->text()));
    if (answer != QMessageBox::Yes) {
        return;
    }

    vaultRequestAsync({{"verb", "delete"}, {"id", vaultSelectedEntryId_.toStdString()}},
                      [this](nlohmann::json response) {
                          if (!response.value("ok", false)) {
                              return;
                          }
                          newVaultEntry();
                          refreshVaultEntryList();
                      });
}

void MainWindow::generateVaultPassword() {
    vaultRequestAsync({{"verb", "generate_password"}, {"length", 20}},
                      [this](nlohmann::json response) {
                          if (!response.value("ok", false) || vaultEntryPassword_ == nullptr) {
                              return;
                          }
                          vaultEntryPassword_->setText(qstr(response.value("password", std::string{})));
                      });
}

void MainWindow::copyVaultPassword() {
    if (vaultEntryPassword_ == nullptr || vaultEntryPassword_->text().isEmpty()) {
        return;
    }
    const QString secret = vaultEntryPassword_->text();
    QGuiApplication::clipboard()->setText(secret);
    vaultClipboardSecret_ = secret;
    vaultClipboardTimer_->start(30000); // 30s clipboard timeout (spec section 2 / ADR-0003)
    statusBar()->showMessage(QStringLiteral("Password copied - clipboard clears in 30s"), 3000);
}

void MainWindow::clearVaultClipboardIfUnchanged() {
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (clipboard->text() == vaultClipboardSecret_) {
        clipboard->clear();
    }
    vaultClipboardSecret_.clear();
}

void MainWindow::showVaultHealth() {
    vaultRequestAsync({{"verb", "health"}}, [this](nlohmann::json response) {
        if (!response.value("ok", false)) {
            QMessageBox::warning(this, QStringLiteral("Vault health"),
                                 qstr(response.value("error", std::string{"unknown error"})));
            return;
        }
        const auto findings = response.value("findings", nlohmann::json::array());
        if (findings.empty()) {
            QMessageBox::information(this, QStringLiteral("Vault health"),
                                     QStringLiteral("No issues found."));
            return;
        }
        QString text;
        for (const auto& f : findings) {
            text += QStringLiteral("- %1: %2\n")
                       .arg(qstr(f.value("title", std::string{})))
                       .arg(qstr(f.value("issue", std::string{})));
        }
        QMessageBox::information(this, QStringLiteral("Vault health"), text);
    });
}

// ----- Network --------------------------------------------------------------

QWidget* MainWindow::buildNetworkPage() {
    auto* page = new QWidget(pages_);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(12);
    layout->addWidget(page_heading(page, QStringLiteral("Network")));

    auto* note = new QLabel(
        QStringLiteral("NexusPC only scans a range you enter yourself - nothing is discovered "
                       "automatically. Enter a CIDR range for your own network (e.g. "
                       "192.168.1.0/24)."),
        page);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: palette(mid);"));
    layout->addWidget(note);

    auto* addRow = new QHBoxLayout();
    networkCidr_ = new QLineEdit(page);
    networkCidr_->setPlaceholderText(QStringLiteral("192.168.1.0/24"));
    networkLabel_ = new QLineEdit(page);
    networkLabel_->setPlaceholderText(QStringLiteral("Label (optional)"));
    auto* addButton = new QPushButton(QStringLiteral("Add range"), page);
    connect(addButton, &QPushButton::clicked, this, &MainWindow::addNetworkRange);
    addRow->addWidget(networkCidr_, 1);
    addRow->addWidget(networkLabel_, 1);
    addRow->addWidget(addButton);
    layout->addLayout(addRow);

    auto* splitter = new QSplitter(page);

    networkList_ = new QListWidget(splitter);
    networkList_->setMaximumWidth(260);
    connect(networkList_, &QListWidget::currentRowChanged, this,
           &MainWindow::networkSelectionChanged);

    auto* right = new QWidget(splitter);
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(12);

    auto* statsRow = new QHBoxLayout();
    statsRow->setSpacing(16);
    networkDevicesCard_ = new StatCard(theme::load_nav_icon(QStringLiteral("network.svg")),
                                       QStringLiteral("Known Devices"), right);
    networkDevicesCard_->setProgress(-1);
    statsRow->addWidget(networkDevicesCard_);
    networkOnlineCard_ = new StatCard(theme::load_nav_icon(QStringLiteral("network.svg")),
                                      QStringLiteral("Online Now"), right);
    networkOnlineCard_->setProgress(-1);
    statsRow->addWidget(networkOnlineCard_);
    rightLayout->addLayout(statsRow);

    auto* scanRow = new QHBoxLayout();
    networkScanButton_ = new QPushButton(QStringLiteral("Scan for devices"), right);
    networkScanButton_->setEnabled(false);
    connect(networkScanButton_, &QPushButton::clicked, this, &MainWindow::startNetworkScan);
    scanRow->addWidget(networkScanButton_);
    scanRow->addStretch(1);
    rightLayout->addLayout(scanRow);

    networkProgress_ = new QProgressBar(right);
    networkProgress_->setRange(0, 100);
    networkProgress_->hide();
    rightLayout->addWidget(networkProgress_);

    networkStatus_ = new QLabel(QStringLiteral("Add a range to get started."), right);
    rightLayout->addWidget(networkStatus_);

    auto* devicesCard = make_card(right);
    auto* devicesCardLayout = new QVBoxLayout(devicesCard);
    devicesCardLayout->setContentsMargins(16, 14, 16, 14);
    networkDevicesTable_ = new QTableWidget(0, 6, devicesCard);
    networkDevicesTable_->setHorizontalHeaderLabels(
        {QStringLiteral("Address"), QStringLiteral("Hostname / label"), QStringLiteral("Status"),
         QStringLiteral("Last seen"), QStringLiteral("Open ports"), QStringLiteral("MAC")});
    networkDevicesTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    networkDevicesTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    networkDevicesTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    networkDevicesTable_->setAlternatingRowColors(true);
    devicesCardLayout->addWidget(networkDevicesTable_);
    rightLayout->addWidget(devicesCard, 1);

    splitter->addWidget(networkList_);
    splitter->addWidget(right);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, 1);

    refreshNetworks();
    return page;
}

void MainWindow::addNetworkRange() {
    if (networkCidr_ == nullptr) {
        return;
    }
    const std::string cidr = networkCidr_->text().trimmed().toStdString();
    if (!nexus::module::network_center::parse_cidr(cidr)) {
        QMessageBox::warning(this, QStringLiteral("Invalid range"),
                             QStringLiteral("Enter a CIDR range like 192.168.1.0/24."));
        return;
    }

    const std::string label = networkLabel_->text().trimmed().toStdString();
    network_.add_network(cidr, label);
    ctx_.audit.record("network_add_range", cidr, label, "desktop");
    networkCidr_->clear();
    networkLabel_->clear();
    refreshNetworks();
}

void MainWindow::refreshNetworks() {
    if (networkList_ == nullptr) {
        return;
    }
    const int previousRow = networkList_->currentRow();
    networkList_->clear();

    for (const auto& range : network_.networks()) {
        const QString label = range.label.empty() ? QString::fromStdString(range.cidr)
                                                   : QString::fromStdString(range.label);
        auto* item = new QListWidgetItem(label + QStringLiteral(" (") +
                                         QString::fromStdString(range.cidr) + QStringLiteral(")"));
        item->setData(Qt::UserRole, static_cast<qlonglong>(range.id));
        networkList_->addItem(item);
    }

    if (networkList_->count() == 0) {
        selectedNetworkId_ = 0;
        networkScanButton_->setEnabled(false);
        refreshDevicesTable();
        return;
    }

    const int row = (previousRow >= 0 && previousRow < networkList_->count()) ? previousRow : 0;
    networkList_->setCurrentRow(row);
    if (row == previousRow) {
        // setCurrentRow won't fire currentRowChanged when the row is unchanged.
        networkSelectionChanged();
    }
}

void MainWindow::networkSelectionChanged() {
    if (networkList_ == nullptr) {
        return;
    }
    QListWidgetItem* item = networkList_->currentItem();
    selectedNetworkId_ = item != nullptr ? item->data(Qt::UserRole).toLongLong() : 0;
    networkScanButton_->setEnabled(selectedNetworkId_ != 0 && !networkScanning_);
    refreshDevicesTable();
}

void MainWindow::refreshDevicesTable() {
    if (networkDevicesTable_ == nullptr) {
        return;
    }
    networkDevicesTable_->setRowCount(0);
    if (selectedNetworkId_ == 0) {
        networkDevicesCard_->setValue(QStringLiteral("-"));
        networkDevicesCard_->setSublabel(QStringLiteral("no range selected"));
        networkOnlineCard_->setValue(QStringLiteral("-"));
        networkOnlineCard_->setSublabel(QStringLiteral("no range selected"));
        return;
    }

    const auto devices = network_.devices(selectedNetworkId_);
    networkDevicesTable_->setRowCount(static_cast<int>(devices.size()));
    int onlineCount = 0;
    int row = 0;
    for (const auto& device : devices) {
        if (device.status == "online") {
            ++onlineCount;
        }
        const QString name = !device.label.empty()   ? QString::fromStdString(device.label)
                             : !device.hostname.empty() ? QString::fromStdString(device.hostname)
                                                         : QString();
        networkDevicesTable_->setItem(row, 0,
                                      new QTableWidgetItem(QString::fromStdString(device.address)));
        networkDevicesTable_->setItem(row, 1, new QTableWidgetItem(name));
        networkDevicesTable_->setItem(row, 2,
                                      new QTableWidgetItem(QString::fromStdString(device.status)));
        networkDevicesTable_->setItem(row, 3, new QTableWidgetItem(format_time(device.last_seen_at)));
        networkDevicesTable_->setItem(
            row, 4,
            new QTableWidgetItem(device.open_ports.empty()
                                     ? QStringLiteral("-")
                                     : QString::fromStdString(device.open_ports)));
        networkDevicesTable_->setItem(
            row, 5,
            new QTableWidgetItem(device.mac.empty() ? QStringLiteral("-")
                                                     : QString::fromStdString(device.mac)));
        ++row;
    }

    networkStatus_->setText(QStringLiteral("%1 known device(s).").arg(devices.size()));
    networkDevicesCard_->setValue(QString::number(devices.size()));
    networkDevicesCard_->setSublabel(QStringLiteral("in this range"));
    networkOnlineCard_->setValue(QString::number(onlineCount));
    networkOnlineCard_->setSublabel(QStringLiteral("of %1 known").arg(devices.size()));
}

void MainWindow::startNetworkScan() {
    if (networkScanning_ || selectedNetworkId_ == 0) {
        return;
    }
    const auto range = network_.find_network(selectedNetworkId_);
    if (!range) {
        return;
    }
    if (!confirmHeavyJob(QStringLiteral("Network scan"))) {
        return;
    }

    networkScanning_ = true;
    networkScanButton_->setEnabled(false);
    networkProgress_->setValue(0);
    networkProgress_->show();
    networkStatus_->setText(QStringLiteral("Scanning…"));
    networkScanCancel_ = std::make_shared<std::atomic<bool>>(false);

    const QPointer<MainWindow> self(this);
    const std::int64_t networkId = selectedNetworkId_;
    const std::string cidr = range->cidr;
    const auto cancel = networkScanCancel_;
    auto* db = &ctx_.db;
    auto* audit = &ctx_.audit;
    auto heavy_lease = std::make_shared<nexus::services::HeavyJobGuard::Lease>(
        ctx_.heavy_jobs.acquire("Network scan"));

    ctx_.pool.submit([self, db, audit, networkId, cidr, cancel, heavy_lease] {
        nexus::module::network_center::NetworkRepository repo(*db);
        nexus::module::network_center::NetworkScanner scanner(repo);

        const auto summary = scanner.scan(
            networkId, cidr,
            [self](nexus::module::network_center::ScanProgress progress) {
                if (progress.total == 0) {
                    return;
                }
                const int percent = static_cast<int>(
                    (static_cast<double>(progress.scanned) / static_cast<double>(progress.total)) *
                    100.0);
                QMetaObject::invokeMethod(
                    qApp,
                    [self, percent] {
                        if (self && self->networkProgress_ != nullptr) {
                            self->networkProgress_->setValue(percent);
                        }
                    },
                    Qt::QueuedConnection);
            },
            [cancel] { return cancel->load(); });

        audit->record("network_scan", cidr,
                      std::to_string(summary.devices_found) + " of " +
                          std::to_string(summary.hosts_probed) + " host(s) responded",
                      "desktop");

        QMetaObject::invokeMethod(
            qApp,
            [self, networkId, found = summary.devices_found, probed = summary.hosts_probed,
             cancelled = summary.cancelled] {
                if (!self) {
                    return;
                }
                self->networkScanning_ = false;
                self->networkProgress_->hide();
                self->networkScanButton_->setEnabled(self->selectedNetworkId_ != 0);
                self->networkStatus_->setText(
                    QStringLiteral("%1 of %2 host(s) responded%3")
                        .arg(found)
                        .arg(probed)
                        .arg(cancelled ? QStringLiteral(" - scan cancelled") : QString()));
                if (self->selectedNetworkId_ == networkId) {
                    self->refreshDevicesTable();
                }
            },
            Qt::QueuedConnection);
    });
}

// ----- Backup -------------------------------------------------------------

QWidget* MainWindow::buildBackupPage() {
    auto* page = new QWidget(pages_);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(12);
    layout->addWidget(page_heading(page, QStringLiteral("Backup")));

    QFont cardTitleFont = page->font();
    cardTitleFont.setBold(true);

    auto* statsRow = new QHBoxLayout();
    statsRow->setSpacing(16);
    backupJobsCard_ = new StatCard(theme::load_nav_icon(QStringLiteral("backup.svg")),
                                   QStringLiteral("Backup Jobs"), page);
    backupJobsCard_->setProgress(-1);
    statsRow->addWidget(backupJobsCard_);
    backupSnapshotsCard_ = new StatCard(theme::load_nav_icon(QStringLiteral("backup.svg")),
                                        QStringLiteral("Snapshots"), page);
    backupSnapshotsCard_->setProgress(-1);
    statsRow->addWidget(backupSnapshotsCard_);
    backupLastSnapshotCard_ = new StatCard(theme::load_nav_icon(QStringLiteral("backup.svg")),
                                           QStringLiteral("Last Snapshot"), page);
    backupLastSnapshotCard_->setProgress(-1);
    statsRow->addWidget(backupLastSnapshotCard_);
    layout->addLayout(statsRow);

    auto* jobsCard = make_card(page);
    auto* jobsCardLayout = new QVBoxLayout(jobsCard);
    jobsCardLayout->setContentsMargins(16, 14, 16, 14);
    auto* jobsBar = new QHBoxLayout();
    auto* jobsTitle = new QLabel(QStringLiteral("Backup jobs"), jobsCard);
    jobsTitle->setFont(cardTitleFont);
    jobsBar->addWidget(jobsTitle);
    jobsBar->addStretch(1);
    auto* newJob = new QPushButton(QStringLiteral("New job…"), jobsCard);
    connect(newJob, &QPushButton::clicked, this, &MainWindow::newBackupJob);
    jobsBar->addWidget(newJob);
    jobsCardLayout->addLayout(jobsBar);

    backupJobsTable_ = new QTableWidget(0, 0, jobsCard);
    configure_table(backupJobsTable_, {QStringLiteral("Name"), QStringLiteral("Source"),
                                       QStringLiteral("Destination"), QStringLiteral("Schedule"),
                                       QStringLiteral("Keep")});
    backupJobsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    backupJobsTable_->setMaximumHeight(180);
    connect(backupJobsTable_, &QTableWidget::itemSelectionChanged, this,
            &MainWindow::refreshBackupSnapshots);
    jobsCardLayout->addWidget(backupJobsTable_);
    layout->addWidget(jobsCard);

    auto* actions = new QHBoxLayout();
    backupRunButton_ = new QPushButton(QStringLiteral("Back up now"), page);
    backupVerifyButton_ = new QPushButton(QStringLiteral("Verify snapshot"), page);
    backupRestoreButton_ = new QPushButton(QStringLiteral("Restore snapshot…"), page);
    backupRestoreFilesButton_ = new QPushButton(QStringLiteral("Restore a file…"), page);
    connect(backupRunButton_, &QPushButton::clicked, this, &MainWindow::runSelectedBackup);
    connect(backupVerifyButton_, &QPushButton::clicked, this,
            &MainWindow::verifySelectedSnapshot);
    connect(backupRestoreButton_, &QPushButton::clicked, this,
            &MainWindow::restoreSelectedSnapshot);
    connect(backupRestoreFilesButton_, &QPushButton::clicked, this,
            &MainWindow::restoreSelectedFiles);
    actions->addWidget(backupRunButton_);
    actions->addWidget(backupVerifyButton_);
    actions->addWidget(backupRestoreButton_);
    actions->addWidget(backupRestoreFilesButton_);
    actions->addStretch(1);
    layout->addLayout(actions);

    backupProgress_ = new QProgressBar(page);
    backupProgress_->setRange(0, 100);
    backupProgress_->hide();
    layout->addWidget(backupProgress_);

    backupStatus_ = new QLabel(page);
    layout->addWidget(backupStatus_);

    auto* snapshotsCard = make_card(page);
    auto* snapshotsCardLayout = new QVBoxLayout(snapshotsCard);
    snapshotsCardLayout->setContentsMargins(16, 14, 16, 14);
    auto* snapshotsTitle = new QLabel(QStringLiteral("Snapshots"), snapshotsCard);
    snapshotsTitle->setFont(cardTitleFont);
    snapshotsCardLayout->addWidget(snapshotsTitle);
    backupSnapshotsTable_ = new QTableWidget(0, 0, snapshotsCard);
    configure_table(backupSnapshotsTable_,
                    {QStringLiteral("Started"), QStringLiteral("State"), QStringLiteral("Files"),
                     QStringLiteral("Total"), QStringLiteral("New")});
    backupSnapshotsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    snapshotsCardLayout->addWidget(backupSnapshotsTable_);
    layout->addWidget(snapshotsCard, 1);

    refreshBackupJobs();
    return page;
}

nexus::core::Uuid MainWindow::selectedBackupJobId() const {
    const auto rows = backupJobsTable_->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
        return {};
    }
    const auto* item = backupJobsTable_->item(rows.first().row(), 0);
    if (item == nullptr) {
        return {};
    }
    return nexus::core::Uuid::parse(item->data(Qt::UserRole).toString().toStdString())
        .value_or(nexus::core::Uuid{});
}

nexus::core::Uuid MainWindow::selectedSnapshotId() const {
    const auto rows = backupSnapshotsTable_->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
        return {};
    }
    const auto* item = backupSnapshotsTable_->item(rows.first().row(), 0);
    if (item == nullptr) {
        return {};
    }
    return nexus::core::Uuid::parse(item->data(Qt::UserRole).toString().toStdString())
        .value_or(nexus::core::Uuid{});
}

void MainWindow::refreshBackupJobs() {
    if (backupJobsTable_ == nullptr) {
        return;
    }
    const auto jobs = backup_.list_jobs();
    backupJobsTable_->setRowCount(static_cast<int>(jobs.size()));
    for (int row = 0; row < static_cast<int>(jobs.size()); ++row) {
        const auto& job = jobs[static_cast<std::size_t>(row)];
        auto* name = new QTableWidgetItem(QString::fromStdString(
            job.name.empty() ? job.source_root : job.name));
        name->setData(Qt::UserRole, QString::fromStdString(job.id.to_string()));
        backupJobsTable_->setItem(row, 0, name);
        backupJobsTable_->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(job.source_root)));
        backupJobsTable_->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(job.destination)));
        backupJobsTable_->setItem(row, 3, new QTableWidgetItem(QString::fromStdString(
                                              job.schedule.empty() ? "manual" : job.schedule)));
        backupJobsTable_->setItem(row, 4, new QTableWidgetItem(QString::number(job.retention_keep)));
    }
    const bool hasJobs = !jobs.empty();
    backupRunButton_->setEnabled(hasJobs && !backupBusy_);
    backupJobsCard_->setValue(QString::number(jobs.size()));
    backupJobsCard_->setSublabel(hasJobs ? QStringLiteral("configured") : QStringLiteral("none yet"));
    refreshBackupSnapshots();
}

void MainWindow::refreshBackupSnapshots() {
    if (backupSnapshotsTable_ == nullptr) {
        return;
    }
    const auto job_id = selectedBackupJobId();
    std::vector<nexus::module::backup::SnapshotRecord> snaps;
    if (!job_id.is_nil()) {
        snaps = backup_.snapshots_for(job_id, 50);
    }
    backupSnapshotsTable_->setRowCount(static_cast<int>(snaps.size()));
    for (int row = 0; row < static_cast<int>(snaps.size()); ++row) {
        const auto& snap = snaps[static_cast<std::size_t>(row)];
        auto* started = new QTableWidgetItem(format_time(snap.started_at));
        started->setData(Qt::UserRole, QString::fromStdString(snap.id.to_string()));
        backupSnapshotsTable_->setItem(row, 0, started);
        backupSnapshotsTable_->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(snap.state)));
        backupSnapshotsTable_->setItem(row, 2, new QTableWidgetItem(QString::number(snap.file_count)));
        backupSnapshotsTable_->setItem(row, 3, new QTableWidgetItem(human_bytes(snap.total_bytes)));
        backupSnapshotsTable_->setItem(row, 4, new QTableWidgetItem(human_bytes(snap.new_bytes)));
    }
    const bool hasSnaps = !snaps.empty();
    backupVerifyButton_->setEnabled(hasSnaps && !backupBusy_);
    backupRestoreButton_->setEnabled(hasSnaps && !backupBusy_);
    backupRestoreFilesButton_->setEnabled(hasSnaps && !backupBusy_);

    backupSnapshotsCard_->setValue(QString::number(snaps.size()));
    backupSnapshotsCard_->setSublabel(job_id.is_nil() ? QStringLiteral("no job selected")
                                                       : QStringLiteral("for selected job"));
    if (hasSnaps) {
        const auto& latest = snaps.front();
        backupLastSnapshotCard_->setValue(QString::fromStdString(latest.state));
        backupLastSnapshotCard_->setSublabel(format_time(latest.started_at));
    } else {
        backupLastSnapshotCard_->setValue(QStringLiteral("-"));
        backupLastSnapshotCard_->setSublabel(job_id.is_nil() ? QStringLiteral("no job selected")
                                                              : QStringLiteral("no snapshots yet"));
    }
}

void MainWindow::newBackupJob() {
    const QString source =
        QFileDialog::getExistingDirectory(this, QStringLiteral("Folder to back up"));
    if (source.isEmpty()) {
        return;
    }

    const auto useNetwork = QMessageBox::question(
        this, QStringLiteral("Backup destination"),
        QStringLiteral("Back up to a network location (a UNC path like "
                       "\\\\server\\share\\backups) instead of browsing a local folder?"));

    QString dest;
    bool ok = false;
    if (useNetwork == QMessageBox::Yes) {
        while (true) {
            dest = QInputDialog::getText(
                this, QStringLiteral("Network backup destination"),
                QStringLiteral("UNC path (e.g. \\\\server\\share\\backups):"), QLineEdit::Normal,
                QString(), &ok);
            if (!ok || dest.isEmpty()) {
                return;
            }
            if (const auto problem =
                    nexus::module::backup::check_destination_reachable(dest.toStdString())) {
                const auto retry = QMessageBox::warning(
                    this, QStringLiteral("Can't reach that destination"),
                    QString::fromStdString(*problem) + QStringLiteral("\n\nTry a different path?"),
                    QMessageBox::Retry | QMessageBox::Cancel);
                if (retry == QMessageBox::Retry) {
                    continue;
                }
                return;
            }
            break;
        }
    } else {
        dest = QFileDialog::getExistingDirectory(this, QStringLiteral("Where to store the backup"));
        if (dest.isEmpty()) {
            return;
        }
    }
    const int keep = QInputDialog::getInt(this, QStringLiteral("Retention"),
                                          QStringLiteral("Keep how many snapshots?"), 10, 1, 999, 1,
                                          &ok);
    if (!ok) {
        return;
    }
    const QString schedule = QInputDialog::getText(
        this, QStringLiteral("Schedule"),
        QStringLiteral("Schedule (blank = manual; e.g. \"every 6h\")"), QLineEdit::Normal, QString(),
        &ok);
    if (!ok) {
        return;
    }

    nexus::module::backup::BackupJob job;
    job.name = QFileInfo(source).fileName().toStdString();
    job.source_root = source.toStdString();
    job.destination = dest.toStdString();
    job.retention_keep = keep;
    job.schedule = schedule.trimmed().toStdString();
    const auto job_id = backup_.upsert_job(job);
    if (backupModule_ != nullptr) {
        backupModule_->reschedule_job(job_id);
    }
    refreshBackupJobs();
    statusBar()->showMessage(QStringLiteral("Backup job created"), 5000);
}

void MainWindow::runSelectedBackup() {
    const auto job_id = selectedBackupJobId();
    if (job_id.is_nil() || backupBusy_) {
        return;
    }
    const auto job = backup_.find_job(job_id);
    if (!job) {
        return;
    }
    if (backupModule_ != nullptr) {
        if (const auto duplicate_bytes = backupModule_->latest_known_duplicate_bytes();
            duplicate_bytes && *duplicate_bytes > 0) {
            const auto answer = QMessageBox::question(
                this, QStringLiteral("Duplicate files found"),
                QStringLiteral("A recent Storage scan found %1 of reclaimable duplicate "
                               "files. Backing them up will use extra space on the "
                               "destination.\n\nBack up %2 anyway?")
                    .arg(human_bytes(*duplicate_bytes), QString::fromStdString(job->name)));
            if (answer != QMessageBox::Yes) {
                return;
            }
        }
    }
    if (!confirmHeavyJob(QStringLiteral("Backup: %1").arg(QString::fromStdString(job->name)))) {
        return;
    }

    backupBusy_ = true;
    backupRunButton_->setEnabled(false);
    backupVerifyButton_->setEnabled(false);
    backupRestoreButton_->setEnabled(false);
    backupProgress_->setValue(0);
    backupProgress_->show();
    backupStatus_->setText(QStringLiteral("Backing up %1…").arg(QString::fromStdString(job->name)));

    const QPointer<MainWindow> self(this);
    auto* db = &ctx_.db;
    const std::filesystem::path source = job->source_root;
    const std::filesystem::path objects = std::filesystem::path(job->destination) / "objects";
    const std::string exclusions = job->exclusions;
    const int keep = job->retention_keep;
    const nexus::core::Uuid id = job_id;
    auto heavy_lease = std::make_shared<nexus::services::HeavyJobGuard::Lease>(
        ctx_.heavy_jobs.acquire("Backup: " + job->name));
    const nexus::jobs::Throttle throttle = currentThrottle();

    ctx_.pool.submit([self, db, source, objects, exclusions, keep, id, heavy_lease, throttle] {
        nexus::module::backup::BackupRepository repo(*db);
        nexus::module::backup::ObjectStore store(objects);
        nexus::module::backup::BackupEngine engine(store, &repo);
        const auto rules = nexus::fs::ExclusionRules::from_text(exclusions);
        const auto summary = engine.run(
            id, source, rules,
            [self](double fraction, std::string_view phase) {
                const int percent = static_cast<int>(fraction * 100.0);
                const QString label =
                    QString::fromUtf8(phase.data(), static_cast<qsizetype>(phase.size()));
                QMetaObject::invokeMethod(
                    qApp,
                    [self, percent, label] {
                        if (self && self->backupProgress_ != nullptr) {
                            self->backupProgress_->setValue(percent);
                            self->backupStatus_->setText(label);
                        }
                    },
                    Qt::QueuedConnection);
            },
            {}, [throttle] { throttle.pace(); });
        repo.prune_snapshots(id, static_cast<std::size_t>(keep < 1 ? 1 : keep));
        const auto referenced = repo.all_referenced_digests();
        store.collect_garbage(
            std::unordered_set<std::string>(referenced.begin(), referenced.end()));

        QMetaObject::invokeMethod(
            qApp,
            [self, files = summary.file_count, newb = summary.new_bytes,
             errs = summary.errors] {
                if (!self) {
                    return;
                }
                self->backupBusy_ = false;
                self->backupProgress_->hide();
                self->backupStatus_->setText(
                    QStringLiteral("Backup done: %1 files, %2 new%3")
                        .arg(files)
                        .arg(human_bytes(newb))
                        .arg(errs > 0 ? QStringLiteral(", %1 error(s)").arg(errs) : QString()));
                self->ctx_.audit.record("backup_run", {},
                                        std::to_string(files) + " files", "desktop");
                self->refreshBackupJobs();
            },
            Qt::QueuedConnection);
    });
}

void MainWindow::verifySelectedSnapshot() {
    const auto snapshot_id = selectedSnapshotId();
    if (snapshot_id.is_nil() || backupBusy_) {
        return;
    }
    const auto job = backup_.find_job(selectedBackupJobId());
    if (!job) {
        return;
    }
    if (!confirmHeavyJob(QStringLiteral("Verify: %1").arg(QString::fromStdString(job->name)))) {
        return;
    }

    backupBusy_ = true;
    backupVerifyButton_->setEnabled(false);
    backupStatus_->setText(QStringLiteral("Verifying…"));

    const QPointer<MainWindow> self(this);
    auto* db = &ctx_.db;
    const std::filesystem::path objects = std::filesystem::path(job->destination) / "objects";
    const nexus::core::Uuid id = snapshot_id;
    auto heavy_lease = std::make_shared<nexus::services::HeavyJobGuard::Lease>(
        ctx_.heavy_jobs.acquire("Verify: " + job->name));

    ctx_.pool.submit([self, db, objects, id, heavy_lease] {
        nexus::module::backup::BackupRepository repo(*db);
        nexus::module::backup::ObjectStore store(objects);
        nexus::module::backup::BackupEngine engine(store, &repo);
        const auto result = engine.verify(id);
        QMetaObject::invokeMethod(
            qApp,
            [self, checked = result.checked, ok = result.ok, corrupt = result.corrupt,
             missing = result.missing] {
                if (!self) {
                    return;
                }
                self->backupBusy_ = false;
                self->refreshBackupSnapshots();
                self->backupStatus_->setText(
                    QStringLiteral("Verify: %1 checked, %2 ok, %3 corrupt, %4 missing")
                        .arg(checked)
                        .arg(ok)
                        .arg(corrupt)
                        .arg(missing));
            },
            Qt::QueuedConnection);
    });
}

void MainWindow::restoreSelectedSnapshot() {
    const auto snapshot_id = selectedSnapshotId();
    if (snapshot_id.is_nil() || backupBusy_) {
        return;
    }
    const auto job = backup_.find_job(selectedBackupJobId());
    if (!job) {
        return;
    }
    const QString target =
        QFileDialog::getExistingDirectory(this, QStringLiteral("Restore into which folder?"));
    if (target.isEmpty()) {
        return;
    }
    if (!confirmHeavyJob(QStringLiteral("Restore: %1").arg(QString::fromStdString(job->name)))) {
        return;
    }
    runRestore(snapshot_id, target, {});
}

void MainWindow::restoreSelectedFiles() {
    const auto snapshot_id = selectedSnapshotId();
    if (snapshot_id.is_nil() || backupBusy_) {
        return;
    }
    const auto job = backup_.find_job(selectedBackupJobId());
    if (!job) {
        return;
    }

    // RestoreEngine already supports restoring one file out of a snapshot
    // (only_path) and this was unit-tested, but nothing in the UI ever
    // exposed it - restoreSelectedSnapshot() always restored everything.
    const auto files = backup_.files_in(snapshot_id);
    if (files.empty()) {
        QMessageBox::information(this, QStringLiteral("Restore a file"),
                                 QStringLiteral("This snapshot has no recorded files."));
        return;
    }

    QStringList items;
    items.reserve(static_cast<int>(files.size()));
    for (const auto& file : files) {
        items << QStringLiteral("%1 (%2)").arg(QString::fromStdString(file.path),
                                               human_bytes(file.size));
    }

    bool ok = false;
    const QString choice =
        QInputDialog::getItem(this, QStringLiteral("Restore a file"),
                              QStringLiteral("File to restore:"), items, 0, false, &ok);
    if (!ok) {
        return;
    }
    const int index = items.indexOf(choice);
    if (index < 0) {
        return;
    }
    const std::string path = files[static_cast<std::size_t>(index)].path;

    const QString target =
        QFileDialog::getExistingDirectory(this, QStringLiteral("Restore into which folder?"));
    if (target.isEmpty()) {
        return;
    }
    if (!confirmHeavyJob(QStringLiteral("Restore file: %1").arg(QString::fromStdString(path)))) {
        return;
    }
    runRestore(snapshot_id, target, path);
}

void MainWindow::runRestore(const nexus::core::Uuid& snapshot_id, const QString& target,
                            std::string_view only_path) {
    const auto job = backup_.find_job(selectedBackupJobId());
    if (!job) {
        return;
    }

    backupBusy_ = true;
    backupRestoreButton_->setEnabled(false);
    backupRestoreFilesButton_->setEnabled(false);
    backupProgress_->setValue(0);
    backupProgress_->show();
    backupStatus_->setText(QStringLiteral("Restoring…"));

    const QPointer<MainWindow> self(this);
    auto* db = &ctx_.db;
    const std::filesystem::path objects = std::filesystem::path(job->destination) / "objects";
    const std::filesystem::path dir = target.toStdWString();
    const nexus::core::Uuid id = snapshot_id;
    const std::string path(only_path);
    auto heavy_lease = std::make_shared<nexus::services::HeavyJobGuard::Lease>(
        ctx_.heavy_jobs.acquire("Restore: " + job->name));

    ctx_.pool.submit([self, db, objects, dir, id, path, heavy_lease] {
        nexus::module::backup::BackupRepository repo(*db);
        nexus::module::backup::ObjectStore store(objects);
        nexus::module::backup::RestoreEngine engine(store, repo);
        const auto result = engine.restore(
            id, dir, path,
            [self](double fraction, std::string_view) {
                const int percent = static_cast<int>(fraction * 100.0);
                QMetaObject::invokeMethod(
                    qApp,
                    [self, percent] {
                        if (self && self->backupProgress_ != nullptr) {
                            self->backupProgress_->setValue(percent);
                        }
                    },
                    Qt::QueuedConnection);
            });
        QMetaObject::invokeMethod(
            qApp,
            [self, restored = result.files_restored, missing = result.missing_blobs] {
                if (!self) {
                    return;
                }
                self->backupBusy_ = false;
                self->backupProgress_->hide();
                self->backupStatus_->setText(
                    QStringLiteral("Restore done: %1 file(s)%2")
                        .arg(restored)
                        .arg(missing > 0 ? QStringLiteral(", %1 missing").arg(missing) : QString()));
                self->ctx_.audit.record("backup_restore", {},
                                        std::to_string(restored) + " files", "desktop");
                self->refreshBackupSnapshots();
            },
            Qt::QueuedConnection);
    });
}

// ----- Search ----------------------------------------------------------------

QWidget* MainWindow::buildSearchPage() {
    auto* page = new QWidget(pages_);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(12);
    layout->addWidget(page_heading(page, QStringLiteral("Search")));

    auto* queryRow = new QHBoxLayout();
    searchQuery_ = new QLineEdit(page);
    searchQuery_->setPlaceholderText(QStringLiteral("Search indexed documents…"));
    searchQuery_->setClearButtonEnabled(true);
    connect(searchQuery_, &QLineEdit::textChanged, this, &MainWindow::runSearchQuery);
    searchIndexButton_ = new QPushButton(QStringLiteral("Index a folder…"), page);
    connect(searchIndexButton_, &QPushButton::clicked, this, &MainWindow::indexFolderForSearch);
    queryRow->addWidget(searchQuery_, 1);
    queryRow->addWidget(searchIndexButton_);
    layout->addLayout(queryRow);

    searchWatchToggle_ = new QCheckBox(
        QStringLiteral("Auto re-index this folder when files change"), page);
    connect(searchWatchToggle_, &QCheckBox::toggled, this, &MainWindow::toggleSearchWatch);
    layout->addWidget(searchWatchToggle_);

    searchProgress_ = new QProgressBar(page);
    searchProgress_->setRange(0, 100);
    searchProgress_->hide();
    layout->addWidget(searchProgress_);

    searchStats_ = new QLabel(page);
    layout->addWidget(searchStats_);

    searchResults_ = new QListWidget(page);
    searchResults_->setWordWrap(true);
    searchResults_->setAlternatingRowColors(true);
    connect(searchResults_, &QListWidget::itemActivated, this, [](QListWidgetItem* item) {
        if (item != nullptr) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(item->data(Qt::UserRole).toString()));
        }
    });
    layout->addWidget(searchResults_, 1);

    searchStats_->setText(QStringLiteral("%1 document(s) indexed")
                              .arg(static_cast<qulonglong>(searchIndexer_->indexed_documents())));
    return page;
}

void MainWindow::runSearchQuery() {
    if (searchResults_ == nullptr || searchBusy_) {
        return;
    }
    const QString text = searchQuery_->text().trimmed();
    searchResults_->clear();
    if (text.isEmpty()) {
        return;
    }

    const auto results = searchIndexer_->query(text.toStdString(), 40);
    for (const auto& result : results) {
        auto* item = new QListWidgetItem(searchResults_);
        item->setData(Qt::UserRole, QString::fromStdString(result.path));
        QString label = QString::fromStdString(result.path);
        if (backupModule_ != nullptr && backupModule_->is_path_backed_up(result.path)) {
            label += QStringLiteral("  [in latest backup]");
        }
        if (storageModule_ != nullptr && storageModule_->is_duplicate_file(result.path)) {
            label += QStringLiteral("  [duplicate]");
        }
        if (!result.snippet.empty()) {
            label += QStringLiteral("\n    ") + QString::fromStdString(result.snippet);
        }
        item->setText(label);
    }
    searchStats_->setText(QStringLiteral("%1 result(s) - %2 document(s) indexed")
                              .arg(results.size())
                              .arg(static_cast<qulonglong>(searchIndexer_->indexed_documents())));
}

void MainWindow::indexFolderForSearch() {
    if (searchBusy_) {
        return;
    }
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Folder to index"));
    if (dir.isEmpty()) {
        return;
    }
    if (!confirmHeavyJob(QStringLiteral("Search indexing"))) {
        return;
    }
    indexFolder(dir.toStdWString());
}

void MainWindow::toggleSearchWatch(bool enabled) {
    if (!enabled) {
        searchWatcher_.reset();
        return;
    }
    if (!searchBusy_ && !searchWatcherRoot_.empty()) {
        startWatchingSearchFolder(searchWatcherRoot_);
    }
    // If indexing is in flight or nothing's been indexed yet this session,
    // indexFolder()'s completion callback arms the watch itself.
}

void MainWindow::startWatchingSearchFolder(const std::filesystem::path& root) {
    if (searchWatcher_ != nullptr && searchWatcherRoot_ == root) {
        return; // already watching this exact folder - leave it running
    }
    searchWatcher_.reset();
    searchWatcherRoot_ = root;

    const QPointer<MainWindow> self(this);
    searchWatcher_ = std::make_unique<nexus::fs::DirectoryWatcher>(
        root, [self, root](const std::vector<nexus::fs::FileChange>&) {
            QMetaObject::invokeMethod(
                qApp,
                [self, root] {
                    if (self && self->searchWatchToggle_ != nullptr &&
                        self->searchWatchToggle_->isChecked() && !self->searchBusy_) {
                        self->indexFolder(root);
                    }
                },
                Qt::QueuedConnection);
        });
    if (!searchWatcher_->start()) {
        searchWatcher_.reset();
        statusBar()->showMessage(
            QStringLiteral("Couldn't watch this folder for changes - auto re-index is off"), 5000);
    }
}

void MainWindow::indexFolder(const std::filesystem::path& root) {
    if (searchBusy_) {
        return;
    }
    searchBusy_ = true;
    searchIndexButton_->setEnabled(false);
    searchQuery_->setEnabled(false);
    searchProgress_->setValue(0);
    searchProgress_->show();
    searchStats_->setText(QStringLiteral("Indexing…"));

    const QPointer<MainWindow> self(this);
    auto* indexer = searchIndexer_.get();
    auto heavy_lease = std::make_shared<nexus::services::HeavyJobGuard::Lease>(
        ctx_.heavy_jobs.acquire("Search indexing"));

    ctx_.pool.submit([self, indexer, root, heavy_lease] {
        const auto summary = indexer->index_tree(
            root, nexus::fs::ExclusionRules::defaults(),
            [self](double fraction, std::string_view phase) {
                const int percent = static_cast<int>(fraction * 100.0);
                const QString label =
                    QString::fromUtf8(phase.data(), static_cast<qsizetype>(phase.size()));
                QMetaObject::invokeMethod(
                    qApp,
                    [self, percent, label] {
                        if (self && self->searchProgress_ != nullptr) {
                            self->searchProgress_->setValue(percent);
                            self->searchStats_->setText(label);
                        }
                    },
                    Qt::QueuedConnection);
            });

        QMetaObject::invokeMethod(
            qApp,
            [self, root, indexed = summary.files_indexed, skipped = summary.files_skipped,
             unchanged = summary.files_unchanged, removed = summary.files_removed] {
                if (!self) {
                    return;
                }
                self->searchBusy_ = false;
                self->searchIndexButton_->setEnabled(true);
                self->searchQuery_->setEnabled(true);
                self->searchProgress_->hide();
                self->ctx_.audit.record(
                    "search_index", {},
                    std::to_string(indexed) + " indexed, " + std::to_string(unchanged) +
                        " unchanged, " + std::to_string(removed) + " removed, " +
                        std::to_string(skipped) + " skipped",
                    "desktop");
                self->searchStats_->setText(
                    QStringLiteral("Indexed %1 file(s), %2 unchanged, %3 removed - %4 document(s) total")
                        .arg(indexed)
                        .arg(unchanged)
                        .arg(removed)
                        .arg(static_cast<qulonglong>(self->searchIndexer_->indexed_documents())));
                self->runSearchQuery();

                if (self->searchWatchToggle_ != nullptr && self->searchWatchToggle_->isChecked()) {
                    self->startWatchingSearchFolder(root);
                }
            },
            Qt::QueuedConnection);
    });
}

} // namespace nexuspc::desktop
