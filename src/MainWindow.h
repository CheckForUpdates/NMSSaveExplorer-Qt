#pragma once

#include <QMainWindow>
#include <QByteArray>
#include <QJsonDocument>
#include <QFutureWatcher>
#include <QHash>
#include <functional>
#include <memory>

#include "core/LosslessJsonDocument.h"
#include "core/BackupManager.h"
#include "core/SaveGameLocator.h"

class QLabel;
class QFileSystemWatcher;
class QStackedWidget;
class QTreeWidget;
class QSplitter;

class InventoryEditorPage;
class JsonExplorerPage;
class SettlementManagerPage;
class ShipManagerPage;
class WelcomePage;
class LoadingOverlay;
class BackupsPage;
class CorvetteManagerPage;
class KnownTechnologyPage;
class KnownProductPage;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    struct LoadResult {
        QJsonDocument doc;
        QString error;
        std::shared_ptr<LosslessJsonDocument> lossless;
    };
    void buildUi();
    void buildMenus();
    void refreshSaveSlots();
    bool confirmRefreshUnload();
    void unloadCurrentSave();
    void browseForSave();
    void browseForSaveDirectory();
    void loadSelectedSave();
    void loadSavePath(const QString &path);
    void openJsonEditor();
    void openInventoryEditor();
    void openCurrenciesEditor();
    void openExpeditionEditor();
    void openStorageManager();
    void openSettlementManager();
    void openShipManager();
    void openCorvetteManager();
    void openMaterialLookup();
    void openKnownTechnologyEditor();
    void openKnownProductEditor();
    void saveChanges();
    void syncOtherSave();
    void undoSync();
    void setStatus(const QString &text);
    void loadSaveInBackground(const QString &path, const QString &statusText,
                              const std::function<void(const LoadResult &)> &onLoaded);
    void selectPage(const QString &key);
    bool ensureSaveLoaded();
    bool hasPendingChanges() const;
    void updateHomeSaveEnabled();
    QString resolveLatestSavePath(const SaveSlot &slot) const;
    bool confirmLeaveJsonEditor(const QString &nextAction);
    bool confirmDiscardOrSave(const QString &pageName,
                              const std::function<bool(QString *)> &saveFn);
    void updateSaveWatcher(const QString &path);
    void handleSaveFileChanged(const QString &path);
    void maybeBackupOnLoad(const QString &path);
    void refreshBackupsPage();
    const SaveSlot *findSlotForPath(const QString &path) const;

    struct PendingSyncTarget {
        QString path;
        QByteArray originalBytes;
    };
    struct PendingSync {
        QString sourcePath;
        QByteArray sourceBytes;
        QList<PendingSyncTarget> targets;
    };

    QSplitter *mainSplitter_ = nullptr;
    QTreeWidget *sectionTree_ = nullptr;
    QStackedWidget *stackedPages_ = nullptr;
    WelcomePage *welcomePage_ = nullptr;
    JsonExplorerPage *jsonPage_ = nullptr;
    InventoryEditorPage *inventoryPage_ = nullptr;
    InventoryEditorPage *currenciesPage_ = nullptr;
    InventoryEditorPage *expeditionPage_ = nullptr;
    InventoryEditorPage *storageManagerPage_ = nullptr;
    SettlementManagerPage *settlementPage_ = nullptr;
    ShipManagerPage *shipManagerPage_ = nullptr;
    CorvetteManagerPage *corvetteManagerPage_ = nullptr;
    KnownTechnologyPage *knownTechnologyPage_ = nullptr;
    KnownProductPage *knownProductPage_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QFileSystemWatcher *saveWatcher_ = nullptr;
    LoadingOverlay *loadingOverlay_ = nullptr;
    BackupsPage *backupsPage_ = nullptr;
    QAction *saveAction_ = nullptr;
    QFutureWatcher<LoadResult> loadingWatcher_;
    bool ignoreNextFileChange_ = false;
    bool syncPending_ = false;
    bool syncUndoAvailable_ = false;
    PendingSync pendingSync_;

    QList<SaveSlot> saveSlots_;
    QString currentSaveFile_;
    QHash<QString, qint64> lastBackupMtime_;
    BackupManager backupManager_;
};
