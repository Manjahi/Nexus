#include "MainWindow.hpp"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
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

#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "ChartWidget.hpp"
#include "NotificationBridge.hpp"

#include "nexus/db/settings_repository.hpp"
#include "nexus/jobs/thread_pool.hpp"
#include "nexus/notify/notification_center.hpp"
#include "nexus/notify/severity.hpp"
#include "nexus/services/audit_log.hpp"
#include "nexus/services/job_repository.hpp"
#include "nexus/jobs/thread_pool.hpp"
#include "nexus/module/backup/backup_engine.hpp"
#include "nexus/module/backup/object_store.hpp"
#include "nexus/module/backup/restore_engine.hpp"
#include "nexus/module/network_center/cidr.hpp"
#include "nexus/module/network_center/network_scanner.hpp"
#include "nexus/module/search/search_indexer.hpp"
#include "nexus/module/storage/duplicate_scanner.hpp"
#include "nexus/module/storage/recycle.hpp"
#include "nexus/fs/exclusion_rules.hpp"
#include "nexus/services/heavy_job_guard.hpp"
#include "nexus/services/module_registry.hpp"
#include "nexus/services/notification_repository.hpp"
#include "nexus/services/report_center.hpp"
#include "nexus/services/service_context.hpp"

namespace nexuspc::desktop {

namespace {

QString qstr(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

QString format_time(const nexus::core::Timestamp& tp) {
    const auto secs = static_cast<qint64>(std::chrono::system_clock::to_time_t(tp));
    return QDateTime::fromSecsSinceEpoch(secs, QTimeZone::UTC)
        .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss 'UTC'"));
}

} // namespace

MainWindow::MainWindow(nexus::services::ServiceContext& context, QString databasePath,
                       NotificationBridge& bridge, QWidget* parent)
    : QMainWindow(parent),
      ctx_(context),
      dbPath_(std::move(databasePath)),
      bridge_(bridge),
      hw_(context.db),
      conn_(context.db),
      storage_(context.db),
      network_(context.db),
      backup_(context.db),
      searchRepo_(context.db),
      searchIndexer_(std::make_unique<nexus::module::search::SearchIndexer>(searchRepo_)) {
    setWindowTitle(QStringLiteral("NexusPC"));
    resize(1100, 720);

    nav_ = new QListWidget(this);
    nav_->setFixedWidth(220);
    nav_->setFrameShape(QFrame::NoFrame);

    pages_ = new QStackedWidget(this);

    addNavPage(QStringLiteral("Home"), buildHomePage());
    addNavPage(QStringLiteral("Alerts"), buildAlertsPage());
    alertsNavRow_ = nav_->count() - 1;
    addNavPage(QStringLiteral("Settings"), buildSettingsPage());
    addNavPage(QStringLiteral("Storage"), buildStoragePage());
    addNavPage(QStringLiteral("Vault"), buildVaultPage());
    vaultNavRow_ = nav_->count() - 1;
    addNavPage(QStringLiteral("Network"), buildNetworkPage());
    addNavPage(QStringLiteral("Internet"), buildInternetPage());
    addNavPage(QStringLiteral("Performance"), buildPerformancePage());
    addNavPage(QStringLiteral("Backup"), buildBackupPage());
    addNavPage(QStringLiteral("Search"), buildSearchPage());
    addNavPage(QStringLiteral("Reports"), buildReportsPage());

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

void MainWindow::addNavPage(const QString& name, QWidget* page) {
    nav_->addItem(name);
    pages_->addWidget(page);
}

QWidget* MainWindow::buildPlaceholderPage(const QString& title, const QString& blurb) {
    auto* page = new QWidget(pages_);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(32, 32, 32, 32);
    layout->setSpacing(12);

    auto* heading = new QLabel(title, page);
    QFont headingFont = heading->font();
    headingFont.setPointSize(headingFont.pointSize() + 8);
    headingFont.setBold(true);
    heading->setFont(headingFont);

    auto* body = new QLabel(blurb, page);
    body->setWordWrap(true);
    body->setStyleSheet(QStringLiteral("color: palette(mid);"));

    layout->addWidget(heading);
    layout->addWidget(body);
    layout->addWidget(new QLabel(QStringLiteral("Not implemented yet."), page));
    layout->addStretch(1);
    return page;
}

QWidget* MainWindow::buildHomePage() {
    auto* page = new QWidget(pages_);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(32, 32, 32, 32);
    layout->setSpacing(16);

    auto* heading = new QLabel(QStringLiteral("Home"), page);
    QFont headingFont = heading->font();
    headingFont.setPointSize(headingFont.pointSize() + 8);
    headingFont.setBold(true);
    heading->setFont(headingFont);
    layout->addWidget(heading);

    auto* form = new QFormLayout();
    homeDbPath_ = new QLabel(page);
    homeDbPath_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    homeModules_ = new QLabel(page);
    homeAlerts_ = new QLabel(page);
    homeJobs_ = new QLabel(page);
    homeLastRun_ = new QLabel(page);
    form->addRow(QStringLiteral("Database"), homeDbPath_);
    form->addRow(QStringLiteral("Modules"), homeModules_);
    form->addRow(QStringLiteral("Active alerts"), homeAlerts_);
    form->addRow(QStringLiteral("Jobs"), homeJobs_);
    form->addRow(QStringLiteral("Latest run"), homeLastRun_);
    layout->addLayout(form);

    auto* buttons = new QHBoxLayout();
    auto* runJob = new QPushButton(QStringLiteral("Run heartbeat job"), page);
    connect(runJob, &QPushButton::clicked, this, &MainWindow::runHeartbeatJob);
    auto* postNote = new QPushButton(QStringLiteral("Post test notification"), page);
    connect(postNote, &QPushButton::clicked, this, &MainWindow::postTestNotification);
    buttons->addWidget(runJob);
    buttons->addWidget(postNote);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    layout->addStretch(1);
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
        auto* check = new QCheckBox(QString::fromStdString(info.display_name), modulesBox);
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

    // UFR-010: per-module retention. Each sampling module reads its own key
    // (once, at startup) - see retention_setting() in hardware/connectivity/
    // network_center's *_module.cpp.
    auto* retentionBox = new QGroupBox(QStringLiteral("Data retention"), page);
    auto* retentionLayout = new QFormLayout(retentionBox);
    const auto add_retention_row = [this, retentionBox, retentionLayout](
                                       const QString& label, const std::string& key,
                                       int default_days) {
        auto* spin = new QSpinBox(retentionBox);
        spin->setRange(1, 3650);
        spin->setSuffix(QStringLiteral(" days"));
        bool ok = false;
        const int stored =
            QString::fromStdString(ctx_.settings.get_or(key, std::to_string(default_days)))
                .toInt(&ok);
        spin->setValue(ok ? stored : default_days);
        connect(spin, &QSpinBox::valueChanged, this,
               [this, key](int value) { ctx_.settings.set(key, std::to_string(value)); });
        retentionLayout->addRow(label, spin);
    };
    add_retention_row(QStringLiteral("Hardware samples"), "retention.hardware.days", 7);
    add_retention_row(QStringLiteral("Connectivity samples"), "retention.connectivity.days", 30);
    add_retention_row(QStringLiteral("Network checks"), "retention.network_center.days", 30);
    layout->addWidget(retentionBox);

    auto* note = new QLabel(
        QStringLiteral("Retention changes take effect the next time NexusPC starts."), page);
    note->setStyleSheet(QStringLiteral("color: palette(mid);"));
    layout->addWidget(note);

    layout->addStretch(1);
    return page;
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
    auto* bar = new QHBoxLayout();
    bar->addWidget(markRead);
    bar->addStretch(1);
    layout->addLayout(bar);

    alertsTable_ = new QTableWidget(0, 4, page);
    alertsTable_->setHorizontalHeaderLabels(
        {QStringLiteral("Time"), QStringLiteral("Severity"), QStringLiteral("Module"),
         QStringLiteral("Title")});
    alertsTable_->horizontalHeader()->setStretchLastSection(true);
    alertsTable_->verticalHeader()->setVisible(false);
    alertsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    alertsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(alertsTable_);

    return page;
}

void MainWindow::refreshHome() {
    if (homeDbPath_ == nullptr) {
        return;
    }
    homeDbPath_->setText(dbPath_);

    const auto& mods = ctx_.modules.modules();
    int enabled = 0;
    for (const auto& info : mods) {
        if (ctx_.modules.is_enabled(info.id)) {
            ++enabled;
        }
    }
    homeModules_->setText(
        QStringLiteral("%1 of %2 enabled").arg(enabled).arg(static_cast<int>(mods.size())));

    const auto unread = static_cast<int>(ctx_.notifications.unread_count());
    homeAlerts_->setText(unread == 0 ? QStringLiteral("none")
                                     : QStringLiteral("%1 unread").arg(unread));

    const auto jobs = ctx_.jobs.list_jobs();
    int runs = 0;
    for (const auto& job : jobs) {
        runs += static_cast<int>(ctx_.jobs.runs_for(job.id, 1000).size());
    }
    homeJobs_->setText(QStringLiteral("%1 job(s), %2 run(s)")
                           .arg(static_cast<int>(jobs.size()))
                           .arg(runs));

    QString latest = QStringLiteral("none");
    for (const auto& job : jobs) {
        if (const auto run = ctx_.jobs.latest_run(job.id)) {
            latest = QStringLiteral("%1/%2 - %3")
                         .arg(QString::fromStdString(job.module),
                              QString::fromStdString(job.kind),
                              qstr(nexus::services::to_string(run->state)));
        }
    }
    homeLastRun_->setText(latest);
}

void MainWindow::refreshAlerts() {
    if (alertsTable_ == nullptr) {
        return;
    }
    const auto notes = ctx_.notifications.recent(200);
    alertsTable_->setRowCount(static_cast<int>(notes.size()));
    for (int row = 0; row < static_cast<int>(notes.size()); ++row) {
        const auto& note = notes[static_cast<std::size_t>(row)];
        auto* time = new QTableWidgetItem(format_time(note.created_at));
        auto* severity = new QTableWidgetItem(qstr(nexus::notify::to_string(note.severity)));
        auto* module = new QTableWidgetItem(QString::fromStdString(note.module));
        auto* title = new QTableWidgetItem(QString::fromStdString(note.title));
        if (!note.is_read()) {
            QFont bold = time->font();
            bold.setBold(true);
            time->setFont(bold);
            severity->setFont(bold);
            module->setFont(bold);
            title->setFont(bold);
        }
        alertsTable_->setItem(row, 0, time);
        alertsTable_->setItem(row, 1, severity);
        alertsTable_->setItem(row, 2, module);
        alertsTable_->setItem(row, 3, title);
    }
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

    cpuChart_ = new ChartWidget(QStringLiteral("CPU load"), 0.0, 1.0, page);
    cpuChart_->setMinimumHeight(200);
    layout->addWidget(cpuChart_);

    memLabel_ = new QLabel(page);
    layout->addWidget(memLabel_);

    procTable_ = new QTableWidget(0, 0, page);
    configure_table(procTable_, {QStringLiteral("Process"), QStringLiteral("PID"),
                                 QStringLiteral("CPU %"), QStringLiteral("Working set (MB)")});
    layout->addWidget(procTable_, 1);
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

    const auto mem = hw_.metric_series("mem.used_fraction", "", since);
    memLabel_->setText(mem.empty()
                           ? QStringLiteral("Memory used: -")
                           : QStringLiteral("Memory used: %1%").arg(mem.back().value * 100.0, 0,
                                                                    'f', 1));

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

    uptimeTable_ = new QTableWidget(0, 0, page);
    configure_table(uptimeTable_,
                    {QStringLiteral("Target"), QStringLiteral("Uptime (last hour)")});
    uptimeTable_->setMaximumHeight(150);
    layout->addWidget(uptimeTable_);

    latencyTarget_ = new QLabel(page);
    layout->addWidget(latencyTarget_);
    latencyChart_ = new ChartWidget(QStringLiteral("Latency (ms)"), 0.0, 50.0, page);
    latencyChart_->setMinimumHeight(180);
    layout->addWidget(latencyChart_);

    layout->addWidget(new QLabel(QStringLiteral("Recent outages"), page));
    outageTable_ = new QTableWidget(0, 0, page);
    configure_table(outageTable_,
                    {QStringLiteral("Target"), QStringLiteral("Started"), QStringLiteral("Ended"),
                     QStringLiteral("Failed samples")});
    layout->addWidget(outageTable_, 1);
    return page;
}

void MainWindow::refreshInternet() {
    if (uptimeTable_ == nullptr) {
        return;
    }
    using nexus::module::connectivity::ProbeKind;
    const auto now = nexus::core::now();
    const auto hour_ago = now - std::chrono::hours{1};

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

QWidget* MainWindow::buildStoragePage() {
    auto* page = new QWidget(pages_);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(12);
    layout->addWidget(page_heading(page, QStringLiteral("Storage")));

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

    storageProgress_ = new QProgressBar(page);
    storageProgress_->setRange(0, 100);
    storageProgress_->hide();
    storagePhase_ = new QLabel(page);
    storagePhase_->hide();
    layout->addWidget(storageProgress_);
    layout->addWidget(storagePhase_);

    storageSummary_ = new QLabel(page);
    layout->addWidget(storageSummary_);

    storageTree_ = new QTreeWidget(page);
    storageTree_->setColumnCount(2);
    storageTree_->setHeaderLabels({QStringLiteral("File"), QStringLiteral("Size")});
    storageTree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    layout->addWidget(storageTree_, 1);

    storageRecycleButton_ = new QPushButton(QStringLiteral("Move checked to Recycle Bin"), page);
    storageRecycleButton_->setEnabled(false);
    connect(storageRecycleButton_, &QPushButton::clicked, this,
            &MainWindow::recycleCheckedDuplicates);
    layout->addWidget(storageRecycleButton_);

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
        return;
    }
    storageSummary_->setText(QStringLiteral("Last scan of %1 - %2 duplicate group(s), %3 reclaimable")
                                 .arg(QString::fromStdString(scan->root))
                                 .arg(scan->duplicate_groups)
                                 .arg(human_bytes(scan->reclaimable_bytes)));
}

void MainWindow::chooseStorageFolder() {
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Choose a folder"));
    if (!dir.isEmpty()) {
        storageFolder_->setText(dir);
        storageScanButton_->setEnabled(!storageScanning_);
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

    ctx_.pool.submit([self, root, cancel, db, heavy_lease] {
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
            [cancel] { return cancel->load(); });

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
    auto* lockButton = new QPushButton(QStringLiteral("Lock now"), vaultUnlockedPanel_);
    connect(lockButton, &QPushButton::clicked, this, &MainWindow::vaultLockNow);
    toolbar->addWidget(newButton);
    toolbar->addWidget(healthButton);
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
    vaultEntryTitle_ = new QLineEdit(detail);
    vaultEntryTitle_->setObjectName(QStringLiteral("vaultEntryTitle"));
    vaultEntryUsername_ = new QLineEdit(detail);
    vaultEntryUsername_->setObjectName(QStringLiteral("vaultEntryUsername"));

    auto* passwordRow = new QHBoxLayout();
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
            auto* item = new QListWidgetItem(
                username.isEmpty() ? title : title + QStringLiteral(" — ") + username);
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

    const nlohmann::json entry = {
        {"id", vaultSelectedEntryId_.toStdString()},
        {"title", vaultEntryTitle_->text().toStdString()},
        {"username", vaultEntryUsername_->text().toStdString()},
        {"password", vaultEntryPassword_->text().toStdString()},
        {"url", vaultEntryUrl_->text().toStdString()},
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

    networkDevicesTable_ = new QTableWidget(0, 4, right);
    networkDevicesTable_->setHorizontalHeaderLabels(
        {QStringLiteral("Address"), QStringLiteral("Hostname / label"), QStringLiteral("Status"),
         QStringLiteral("Last seen")});
    networkDevicesTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    networkDevicesTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    networkDevicesTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    rightLayout->addWidget(networkDevicesTable_, 1);

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
        return;
    }

    const auto devices = network_.devices(selectedNetworkId_);
    networkDevicesTable_->setRowCount(static_cast<int>(devices.size()));
    int row = 0;
    for (const auto& device : devices) {
        const QString name = !device.label.empty()   ? QString::fromStdString(device.label)
                             : !device.hostname.empty() ? QString::fromStdString(device.hostname)
                                                         : QString();
        networkDevicesTable_->setItem(row, 0,
                                      new QTableWidgetItem(QString::fromStdString(device.address)));
        networkDevicesTable_->setItem(row, 1, new QTableWidgetItem(name));
        networkDevicesTable_->setItem(row, 2,
                                      new QTableWidgetItem(QString::fromStdString(device.status)));
        networkDevicesTable_->setItem(row, 3, new QTableWidgetItem(format_time(device.last_seen_at)));
        ++row;
    }

    networkStatus_->setText(QStringLiteral("%1 known device(s).").arg(devices.size()));
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

    auto* jobsBar = new QHBoxLayout();
    jobsBar->addWidget(new QLabel(QStringLiteral("Backup jobs"), page));
    jobsBar->addStretch(1);
    auto* newJob = new QPushButton(QStringLiteral("New job…"), page);
    connect(newJob, &QPushButton::clicked, this, &MainWindow::newBackupJob);
    jobsBar->addWidget(newJob);
    layout->addLayout(jobsBar);

    backupJobsTable_ = new QTableWidget(0, 0, page);
    configure_table(backupJobsTable_, {QStringLiteral("Name"), QStringLiteral("Source"),
                                       QStringLiteral("Destination"), QStringLiteral("Schedule"),
                                       QStringLiteral("Keep")});
    backupJobsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    backupJobsTable_->setMaximumHeight(180);
    connect(backupJobsTable_, &QTableWidget::itemSelectionChanged, this,
            &MainWindow::refreshBackupSnapshots);
    layout->addWidget(backupJobsTable_);

    auto* actions = new QHBoxLayout();
    backupRunButton_ = new QPushButton(QStringLiteral("Back up now"), page);
    backupVerifyButton_ = new QPushButton(QStringLiteral("Verify snapshot"), page);
    backupRestoreButton_ = new QPushButton(QStringLiteral("Restore snapshot…"), page);
    connect(backupRunButton_, &QPushButton::clicked, this, &MainWindow::runSelectedBackup);
    connect(backupVerifyButton_, &QPushButton::clicked, this,
            &MainWindow::verifySelectedSnapshot);
    connect(backupRestoreButton_, &QPushButton::clicked, this,
            &MainWindow::restoreSelectedSnapshot);
    actions->addWidget(backupRunButton_);
    actions->addWidget(backupVerifyButton_);
    actions->addWidget(backupRestoreButton_);
    actions->addStretch(1);
    layout->addLayout(actions);

    backupProgress_ = new QProgressBar(page);
    backupProgress_->setRange(0, 100);
    backupProgress_->hide();
    layout->addWidget(backupProgress_);

    backupStatus_ = new QLabel(page);
    layout->addWidget(backupStatus_);

    layout->addWidget(new QLabel(QStringLiteral("Snapshots"), page));
    backupSnapshotsTable_ = new QTableWidget(0, 0, page);
    configure_table(backupSnapshotsTable_,
                    {QStringLiteral("Started"), QStringLiteral("State"), QStringLiteral("Files"),
                     QStringLiteral("Total"), QStringLiteral("New")});
    backupSnapshotsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(backupSnapshotsTable_, 1);

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
}

void MainWindow::newBackupJob() {
    const QString source =
        QFileDialog::getExistingDirectory(this, QStringLiteral("Folder to back up"));
    if (source.isEmpty()) {
        return;
    }
    const QString dest =
        QFileDialog::getExistingDirectory(this, QStringLiteral("Where to store the backup"));
    if (dest.isEmpty()) {
        return;
    }
    bool ok = false;
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
    backup_.upsert_job(job);
    refreshBackupJobs();
    statusBar()->showMessage(QStringLiteral("Backup job created (restart to activate a schedule)"),
                             5000);
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

    ctx_.pool.submit([self, db, source, objects, exclusions, keep, id, heavy_lease] {
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
            });
        repo.prune_snapshots(id, static_cast<std::size_t>(keep < 1 ? 1 : keep));

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

    backupBusy_ = true;
    backupRestoreButton_->setEnabled(false);
    backupProgress_->setValue(0);
    backupProgress_->show();
    backupStatus_->setText(QStringLiteral("Restoring…"));

    const QPointer<MainWindow> self(this);
    auto* db = &ctx_.db;
    const std::filesystem::path objects = std::filesystem::path(job->destination) / "objects";
    const std::filesystem::path dir = target.toStdWString();
    const nexus::core::Uuid id = snapshot_id;
    auto heavy_lease = std::make_shared<nexus::services::HeavyJobGuard::Lease>(
        ctx_.heavy_jobs.acquire("Restore: " + job->name));

    ctx_.pool.submit([self, db, objects, dir, id, heavy_lease] {
        nexus::module::backup::BackupRepository repo(*db);
        nexus::module::backup::ObjectStore store(objects);
        nexus::module::backup::RestoreEngine engine(store, repo);
        const auto result = engine.restore(
            id, dir, {},
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

    searchBusy_ = true;
    searchIndexButton_->setEnabled(false);
    searchQuery_->setEnabled(false);
    searchProgress_->setValue(0);
    searchProgress_->show();
    searchStats_->setText(QStringLiteral("Indexing…"));

    const QPointer<MainWindow> self(this);
    const std::filesystem::path root = dir.toStdWString();
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
            [self, indexed = summary.files_indexed, skipped = summary.files_skipped] {
                if (!self) {
                    return;
                }
                self->searchBusy_ = false;
                self->searchIndexButton_->setEnabled(true);
                self->searchQuery_->setEnabled(true);
                self->searchProgress_->hide();
                self->ctx_.audit.record("search_index", {},
                                        std::to_string(indexed) + " indexed, " +
                                            std::to_string(skipped) + " skipped",
                                        "desktop");
                self->searchStats_->setText(
                    QStringLiteral("Indexed %1 file(s) - %2 document(s) total")
                        .arg(indexed)
                        .arg(static_cast<qulonglong>(self->searchIndexer_->indexed_documents())));
                self->runSearchQuery();
            },
            Qt::QueuedConnection);
    });
}

} // namespace nexuspc::desktop
