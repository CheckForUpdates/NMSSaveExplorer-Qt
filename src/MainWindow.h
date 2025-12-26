#pragma once

#include <QMainWindow>
#include <QJsonDocument>
#include <QFutureWatcher>
#include <functional>
#include <memory>

#include "core/LosslessJsonDocument.h"
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
    void browseForSave();
    void loadSelectedSave();
    void loadSavePath(const QString &path);
    void openJsonEditor();
    void openInventoryEditor();
    void openCurrenciesEditor();
    void openExpeditionEditor();
    void openStorageManager();
    void openSettlementManager();
    void openShipManager();
    void openMaterialLookup();
    void saveChanges();
    void setStatus(const QString &text);
    void selectPage(const QString &key);
    QString resolveLatestSavePath(const SaveSlot &slot) const;
    bool confirmLeaveJsonEditor(const QString &nextAction);
    bool confirmDiscardOrSave(const QString &pageName,
                              const std::function<bool(QString *)> &saveFn);
    void updateSaveWatcher(const QString &path);
    void handleSaveFileChanged(const QString &path);


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
    QLabel *statusLabel_ = nullptr;
    QFileSystemWatcher *saveWatcher_ = nullptr;
    LoadingOverlay *loadingOverlay_ = nullptr;
    QFutureWatcher<LoadResult> loadingWatcher_;
    bool ignoreNextFileChange_ = false;

    QList<SaveSlot> saveSlots_;
    QString currentSaveFile_;
};
