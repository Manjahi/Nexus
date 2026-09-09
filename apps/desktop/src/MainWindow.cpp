#include "MainWindow.hpp"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFont>
#include <QLineEdit>
#include <QCoreApplication>
#include <QMessageBox>
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

#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
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
#include "nexus/module/storage/duplicate_scanner.hpp"
#include "nexus/module/storage/recycle.hpp"
#include "nexus/fs/exclusion_rules.hpp"
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
      storage_(context.db) {
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
    addNavPage(QStringLiteral("Network"),
               buildPlaceholderPage(QStringLiteral("Network"),
                                    QStringLiteral("Device map, uptime, latency, alerts.")));
    addNavPage(QStringLiteral("Internet"), buildInternetPage());
    addNavPage(QStringLiteral("Performance"), buildPerformancePage());
    addNavPage(QStringLiteral("Backup"),
               buildPlaceholderPage(QStringLiteral("Backup"),
                                    QStringLiteral("Jobs, snapshots, retention, restore.")));
    addNavPage(QStringLiteral("Search"),
               buildPlaceholderPage(QStringLiteral("Search"),
                                    QStringLiteral("Query, filters, results, indexing controls.")));
    addNavPage(QStringLiteral("Reports"), buildReportsPage());

    connect(nav_, &QListWidget::currentRowChanged, pages_, &QStackedWidget::setCurrentIndex);
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

    auto* form = new QFormLayout();
    auto* retention = new QSpinBox(page);
    retention->setRange(1, 3650);
    retention->setSuffix(QStringLiteral(" days"));
    bool ok = false;
    const int stored =
        QString::fromStdString(ctx_.settings.get_or("retention.days", "30")).toInt(&ok);
    retention->setValue(ok ? stored : 30);
    connect(retention, &QSpinBox::valueChanged, this, [this](int value) {
        ctx_.settings.set("retention.days", std::to_string(value));
    });
    form->addRow(QStringLiteral("Data retention"), retention);
    layout->addLayout(form);

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

    ctx_.pool.submit([self, root, cancel, db] {
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

} // namespace nexuspc::desktop
