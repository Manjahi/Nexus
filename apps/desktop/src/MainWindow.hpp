#pragma once

#include <QMainWindow>

#include <atomic>
#include <memory>

#include "nexus/core/id.hpp"
#include "nexus/module/backup/backup_repository.hpp"
#include "nexus/module/connectivity/connectivity_repository.hpp"
#include "nexus/module/hardware/hardware_repository.hpp"
#include "nexus/module/storage/storage_repository.hpp"

class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class QTreeWidget;

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
    QWidget* buildStoragePage();
    QWidget* buildBackupPage();
    QWidget* buildPlaceholderPage(const QString& title, const QString& blurb);
    void addNavPage(const QString& name, QWidget* page);

    void chooseStorageFolder();
    void startStorageScan();
    void applyScanResults(const nexus::module::storage::ScanSummary& summary);
    void recycleCheckedDuplicates();
    void refreshStorageSummary();

    void refreshBackupJobs();
    void refreshBackupSnapshots();
    void newBackupJob();
    void runSelectedBackup();
    void verifySelectedSnapshot();
    void restoreSelectedSnapshot();
    [[nodiscard]] nexus::core::Uuid selectedBackupJobId() const;
    [[nodiscard]] nexus::core::Uuid selectedSnapshotId() const;

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
    nexus::module::storage::StorageRepository storage_;
    nexus::module::backup::BackupRepository backup_;

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

    QLineEdit* storageFolder_{nullptr};
    QPushButton* storageScanButton_{nullptr};
    QPushButton* storageRecycleButton_{nullptr};
    QProgressBar* storageProgress_{nullptr};
    QLabel* storagePhase_{nullptr};
    QLabel* storageSummary_{nullptr};
    QTreeWidget* storageTree_{nullptr};
    std::shared_ptr<std::atomic<bool>> storageCancel_;
    bool storageScanning_{false};

    QTableWidget* backupJobsTable_{nullptr};
    QTableWidget* backupSnapshotsTable_{nullptr};
    QLabel* backupStatus_{nullptr};
    QProgressBar* backupProgress_{nullptr};
    QPushButton* backupRunButton_{nullptr};
    QPushButton* backupVerifyButton_{nullptr};
    QPushButton* backupRestoreButton_{nullptr};
    bool backupBusy_{false};

    nexus::core::Uuid heartbeatJobId_{};
};

} // namespace nexuspc::desktop
