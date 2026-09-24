#pragma once

#include <QMainWindow>

#include <nlohmann/json_fwd.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

#include "nexus/core/id.hpp"
#include "nexus/fs/directory_watcher.hpp"
#include "nexus/jobs/throttle.hpp"
#include "nexus/module/backup/backup_repository.hpp"
#include "nexus/module/connectivity/connectivity_repository.hpp"
#include "nexus/module/hardware/hardware_repository.hpp"
#include "nexus/module/network_center/network_repository.hpp"
#include "nexus/module/search/search_indexer.hpp"
#include "nexus/module/search/search_repository.hpp"
#include "nexus/module/storage/storage_repository.hpp"
#include "nexus/notify/notification.hpp"

#include "VaultClient.hpp"

class QCheckBox;
class QCloseEvent;
class QComboBox;
class QFormLayout;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class QTimer;
class QTreeWidget;

namespace nexus::services {
struct ServiceContext;
}

namespace nexus::module::backup {
class BackupModule;
}
namespace nexus::module::storage {
class StorageModule;
}

namespace nexuspc::desktop {

class ChartWidget;
class NotificationBridge;

/// Application shell: left-hand navigation bound to a stack of pages, one
/// real page per module - none are placeholders.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(nexus::services::ServiceContext& context, QString databasePath,
               NotificationBridge& bridge, nexus::module::backup::BackupModule* backupModule,
               nexus::module::storage::StorageModule* storageModule, QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    QWidget* buildHomePage();
    QWidget* buildSettingsPage();
    QWidget* buildAlertsPage();
    QWidget* buildPerformancePage();
    QWidget* buildInternetPage();
    QWidget* buildReportsPage();
    QWidget* buildStoragePage();
    QWidget* buildVaultPage();
    QWidget* buildNetworkPage();
    QWidget* buildBackupPage();
    QWidget* buildSearchPage();
    void addNavPage(const QString& iconName, const QString& name, QWidget* page);
    void showAboutDialog();

    void chooseStorageFolder();
    void startStorageScan();
    void applyScanResults(const nexus::module::storage::ScanSummary& summary);
    void recycleCheckedDuplicates();
    void refreshStorageSummary();
    void toggleStorageWatch(bool enabled);
    void startWatchingStorageFolder(const std::filesystem::path& root);

    /// UFR-018: if another heavy job is already running, asks the user
    /// whether to proceed anyway. True means go ahead (nothing was running,
    /// or the user confirmed); false means the caller should abort.
    [[nodiscard]] bool confirmHeavyJob(const QString& label);

    /// UFR-017: the throttle level currently configured in Settings, read
    /// fresh for each job (not live-reloaded mid-job).
    [[nodiscard]] nexus::jobs::Throttle currentThrottle() const;

    void vaultRequestAsync(nlohmann::json body, std::function<void(nlohmann::json)> onDone);
    void refreshVaultStatus();
    void applyVaultStatus(const nlohmann::json& status);
    void vaultUnlockOrCreate();
    void vaultLockNow();
    void newVaultEntry();
    void vaultSelectionChanged();
    void loadVaultEntry(const QString& id);
    void saveVaultEntry();
    void deleteVaultEntry();
    void generateVaultPassword();
    void copyVaultPassword();
    void showVaultHealth();
    void refreshVaultEntryList();
    void clearVaultClipboardIfUnchanged();
    void vaultEntryKindChanged(int index);
    void exportVault();

    void addNetworkRange();
    void refreshNetworks();
    void networkSelectionChanged();
    void startNetworkScan();
    void refreshDevicesTable();

    void refreshBackupJobs();
    void refreshBackupSnapshots();
    void newBackupJob();
    void runSelectedBackup();
    void verifySelectedSnapshot();
    void restoreSelectedSnapshot();
    void restoreSelectedFiles();
    void runRestore(const nexus::core::Uuid& snapshot_id, const QString& target,
                    std::string_view only_path);
    [[nodiscard]] nexus::core::Uuid selectedBackupJobId() const;
    [[nodiscard]] nexus::core::Uuid selectedSnapshotId() const;

    void runSearchQuery();
    void indexFolderForSearch();
    void indexFolder(const std::filesystem::path& root);
    void toggleSearchWatch(bool enabled);
    void startWatchingSearchFolder(const std::filesystem::path& root);

    void refreshHome();
    void refreshAlerts();
    void showAlertDetails(int row);
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
    nexus::module::network_center::NetworkRepository network_;
    nexus::module::backup::BackupRepository backup_;
    nexus::module::backup::BackupModule* backupModule_{nullptr};
    nexus::module::storage::StorageModule* storageModule_{nullptr};
    nexus::module::search::SearchRepository searchRepo_;
    std::unique_ptr<nexus::module::search::SearchIndexer> searchIndexer_;

    QListWidget* nav_{nullptr};
    QStackedWidget* pages_{nullptr};
    int alertsNavRow_{-1};

    QLabel* homeDbPath_{nullptr};
    QLabel* homeModules_{nullptr};
    QLabel* homeAlerts_{nullptr};
    QLabel* homeJobs_{nullptr};
    QLabel* homeLastRun_{nullptr};
    QLabel* homeHealth_{nullptr};

    QTableWidget* alertsTable_{nullptr};
    QPushButton* alertsDetailsButton_{nullptr};
    QComboBox* alertsPriorityFilter_{nullptr};
    std::vector<nexus::notify::Notification> alertsRows_;
    /// Row indices into alertsRows_ that alertsTable_ currently displays,
    /// in display order - lets showAlertDetails() map a clicked table row
    /// back to the right notification when the priority filter hides some.
    std::vector<int> alertsVisibleRows_;

    ChartWidget* cpuChart_{nullptr};
    QLabel* memLabel_{nullptr};
    QLabel* netLabel_{nullptr};
    QLabel* batteryLabel_{nullptr};
    QTableWidget* procTable_{nullptr};

    QTableWidget* uptimeTable_{nullptr};
    ChartWidget* latencyChart_{nullptr};
    QLabel* pathStatusLabel_{nullptr};
    QLabel* latencyTarget_{nullptr};
    QLabel* speedTestLabel_{nullptr};
    ChartWidget* speedTestChart_{nullptr};
    QTableWidget* outageTable_{nullptr};

    QTableWidget* reportsTable_{nullptr};

    QLineEdit* storageFolder_{nullptr};
    QPushButton* storageScanButton_{nullptr};
    QPushButton* storageRecycleButton_{nullptr};
    QProgressBar* storageProgress_{nullptr};
    QLabel* storagePhase_{nullptr};
    QLabel* storageSummary_{nullptr};
    QProgressBar* storageUsageBar_{nullptr};
    QTreeWidget* storageTree_{nullptr};
    QTableWidget* storageHistoryTable_{nullptr};
    std::shared_ptr<std::atomic<bool>> storageCancel_;
    bool storageScanning_{false};
    QCheckBox* storageWatchToggle_{nullptr};
    std::unique_ptr<nexus::fs::DirectoryWatcher> storageWatcher_;
    std::filesystem::path storageWatcherRoot_;

    VaultClient vault_;
    QLabel* vaultStatus_{nullptr};
    QWidget* vaultLockedPanel_{nullptr};
    QLineEdit* vaultPasswordInput_{nullptr};
    QLineEdit* vaultPasswordConfirm_{nullptr};
    QLabel* vaultConfirmLabel_{nullptr};
    QPushButton* vaultUnlockButton_{nullptr};
    QWidget* vaultUnlockedPanel_{nullptr};
    QListWidget* vaultEntryList_{nullptr};
    QFormLayout* vaultForm_{nullptr};
    QComboBox* vaultEntryKind_{nullptr};
    QLineEdit* vaultEntryTitle_{nullptr};
    QLineEdit* vaultEntryUsername_{nullptr};
    QLineEdit* vaultEntryPassword_{nullptr};
    QHBoxLayout* vaultPasswordRow_{nullptr};
    QLineEdit* vaultEntryUrl_{nullptr};
    QLineEdit* vaultEntryTags_{nullptr};
    QPlainTextEdit* vaultEntryNotes_{nullptr};
    QPushButton* vaultSaveButton_{nullptr};
    QPushButton* vaultDeleteButton_{nullptr};
    QString vaultSelectedEntryId_;
    bool vaultCreateMode_{false};
    bool vaultBusy_{false};
    QTimer* vaultClipboardTimer_{nullptr};
    QString vaultClipboardSecret_;
    int vaultNavRow_{-1};
    bool vaultStatusLoaded_{false};

    QLineEdit* networkCidr_{nullptr};
    QLineEdit* networkLabel_{nullptr};
    QListWidget* networkList_{nullptr};
    QPushButton* networkScanButton_{nullptr};
    QProgressBar* networkProgress_{nullptr};
    QLabel* networkStatus_{nullptr};
    QTableWidget* networkDevicesTable_{nullptr};
    std::int64_t selectedNetworkId_{0};
    bool networkScanning_{false};
    std::shared_ptr<std::atomic<bool>> networkScanCancel_;

    QTableWidget* backupJobsTable_{nullptr};
    QTableWidget* backupSnapshotsTable_{nullptr};
    QLabel* backupStatus_{nullptr};
    QProgressBar* backupProgress_{nullptr};
    QPushButton* backupRunButton_{nullptr};
    QPushButton* backupVerifyButton_{nullptr};
    QPushButton* backupRestoreButton_{nullptr};
    QPushButton* backupRestoreFilesButton_{nullptr};
    bool backupBusy_{false};

    QLineEdit* searchQuery_{nullptr};
    QPushButton* searchIndexButton_{nullptr};
    QProgressBar* searchProgress_{nullptr};
    QLabel* searchStats_{nullptr};
    QListWidget* searchResults_{nullptr};
    bool searchBusy_{false};
    QCheckBox* searchWatchToggle_{nullptr};
    std::unique_ptr<nexus::fs::DirectoryWatcher> searchWatcher_;
    std::filesystem::path searchWatcherRoot_;

    nexus::core::Uuid heartbeatJobId_{};
};

} // namespace nexuspc::desktop
