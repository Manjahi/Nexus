#pragma once

#include <QMainWindow>

#include "nexus/core/id.hpp"
#include "nexus/module/connectivity/connectivity_repository.hpp"
#include "nexus/module/hardware/hardware_repository.hpp"

class QLabel;
class QListWidget;
class QStackedWidget;
class QTableWidget;

namespace nexus::services {
struct ServiceContext;
}

namespace nexuspc::desktop {

class ChartWidget;
class NotificationBridge;

/// Application shell: left-hand navigation bound to a stack of pages. Home,
/// Settings, Alerts, Performance, and Internet are wired to the platform
/// services; the remaining module pages are placeholders.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(nexus::services::ServiceContext& context, QString databasePath,
               NotificationBridge& bridge, QWidget* parent = nullptr);

private:
    QWidget* buildHomePage();
    QWidget* buildSettingsPage();
    QWidget* buildAlertsPage();
    QWidget* buildPerformancePage();
    QWidget* buildInternetPage();
    QWidget* buildReportsPage();
    QWidget* buildPlaceholderPage(const QString& title, const QString& blurb);
    void addNavPage(const QString& name, QWidget* page);

    void refreshHome();
    void refreshAlerts();
    void refreshPerformance();
    void refreshInternet();
    void refreshReports();
    void generateReport(const QString& kind, bool csv);
    void updateAlertsNavLabel();
    void runHeartbeatJob();
    void postTestNotification();

    nexus::services::ServiceContext& ctx_;
    QString dbPath_;
    NotificationBridge& bridge_;
    nexus::module::hardware::HardwareRepository hw_;
    nexus::module::connectivity::ConnectivityRepository conn_;

    QListWidget* nav_{nullptr};
    QStackedWidget* pages_{nullptr};
    int alertsNavRow_{-1};

    QLabel* homeDbPath_{nullptr};
    QLabel* homeModules_{nullptr};
    QLabel* homeAlerts_{nullptr};
    QLabel* homeJobs_{nullptr};
    QLabel* homeLastRun_{nullptr};

    QTableWidget* alertsTable_{nullptr};

    ChartWidget* cpuChart_{nullptr};
    QLabel* memLabel_{nullptr};
    QTableWidget* procTable_{nullptr};

    QTableWidget* uptimeTable_{nullptr};
    ChartWidget* latencyChart_{nullptr};
    QLabel* latencyTarget_{nullptr};
    QTableWidget* outageTable_{nullptr};

    QTableWidget* reportsTable_{nullptr};

    nexus::core::Uuid heartbeatJobId_{};
};

} // namespace nexuspc::desktop
