#pragma once

#include <QMainWindow>

#include "nexus/core/id.hpp"

class QLabel;
class QListWidget;
class QStackedWidget;
class QTableWidget;

namespace nexus::services {
struct ServiceContext;
}

namespace nexuspc::desktop {

class NotificationBridge;

/// Application shell: left-hand navigation bound to a stack of pages. Home,
/// Settings, and Alerts are wired to the platform services; the remaining
/// module pages are placeholders until their milestones land.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(nexus::services::ServiceContext& context, QString databasePath,
               NotificationBridge& bridge, QWidget* parent = nullptr);

private:
    QWidget* buildHomePage();
    QWidget* buildSettingsPage();
    QWidget* buildAlertsPage();
    QWidget* buildPlaceholderPage(const QString& title, const QString& blurb);
    void addNavPage(const QString& name, QWidget* page);

    void refreshHome();
    void refreshAlerts();
    void updateAlertsNavLabel();
    void runHeartbeatJob();
    void postTestNotification();

    nexus::services::ServiceContext& ctx_;
    QString dbPath_;
    NotificationBridge& bridge_;

    QListWidget* nav_{nullptr};
    QStackedWidget* pages_{nullptr};
    int alertsNavRow_{-1};

    QLabel* homeDbPath_{nullptr};
    QLabel* homeModules_{nullptr};
    QLabel* homeAlerts_{nullptr};
    QLabel* homeJobs_{nullptr};
    QLabel* homeLastRun_{nullptr};

    QTableWidget* alertsTable_{nullptr};

    nexus::core::Uuid heartbeatJobId_{};
};

} // namespace nexuspc::desktop
