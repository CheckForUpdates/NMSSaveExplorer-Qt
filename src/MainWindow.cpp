#include "MainWindow.h"

#include "core/LosslessJsonDocument.h"
#include "core/SaveCache.h"
#include "inventory/InventoryEditorPage.h"
#include "inventory/InventoryGridWidget.h"
#include "inventory/KnownTechnologyPage.h"
#include "inventory/KnownProductPage.h"
#include "registry/ItemCatalog.h"
#include "registry/ItemDefinitionRegistry.h"
#include "registry/LocalizationRegistry.h"
#include "settlement/SettlementManagerPage.h"
#include "ship/ShipManagerPage.h"
#include "ui/BackupsPage.h"
#include "ui/JsonExplorerPage.h"
#include "ui/MaterialLookupDialog.h"
#include "ui/WelcomePage.h"
#include "ui/LoadingOverlay.h"
#include "frigate/FrigateManagerPage.h"
#include <QtConcurrent>
#include <QAction>
#include <QAbstractItemView>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileSystemWatcher>
#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QFrame>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <memory>

namespace {
const char *kPageHome = "home";
const char *kPageJson = "json";
const char *kPageInventory = "inventory";
const char *kPageCurrencies = "currencies";
const char *kPageExpedition = "expedition";
const char *kPageStorage = "storage";
const char *kPageSettlement = "settlement";
const char *kPageShip = "ship";
const char *kPageFrigateTemplate = "frigateTemplate";
const char *kPageBackups = "backups";
const char *kPageKnownTechnology = "known-technology";
const char *kPageKnownProduct = "known-product";
const char *kPageMaterialLookup = "material-lookup";

class MenuIndentDelegate : public QStyledItemDelegate
{
public:
    explicit MenuIndentDelegate(int leftPadding, int perLevelIndent = 0, QObject *parent = nullptr)
        : QStyledItemDelegate(parent)
        , leftPadding_(leftPadding)
        , perLevelIndent_(perLevelIndent)
    {
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt(option);
        opt.rect.adjust(leftPadding_ + depthIndent(index), 0, 0, 0);
        QStyledItemDelegate::paint(painter, opt, index);
    }

    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.rwidth() += leftPadding_ + depthIndent(index);
        return size;
    }

private:
    int depthIndent(const QModelIndex &index) const
    {
        int depth = 0;
        QModelIndex parent = index.parent();
        while (parent.isValid()) {
            ++depth;
            parent = parent.parent();
        }
        return depth * perLevelIndent_;
    }

    int leftPadding_ = 0;
    int perLevelIndent_ = 0;
};

}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("NMSSaveExplorer-Qt");
    buildUi();
}

void MainWindow::buildUi()
{
    mainSplitter_ = new QSplitter(this);
    setCentralWidget(mainSplitter_);
    mainSplitter_->setChildrenCollapsible(false);
    mainSplitter_->setHandleWidth(0);

    sectionTree_ = new QTreeWidget(mainSplitter_);
    sectionTree_->setHeaderHidden(true);
    sectionTree_->setMinimumWidth(220);
    sectionTree_->setMaximumWidth(220);
    sectionTree_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    sectionTree_->setIndentation(0);
    sectionTree_->setRootIsDecorated(false);
    sectionTree_->setItemDelegate(new MenuIndentDelegate(12, 12, sectionTree_));
    {
        QFont menuFont = sectionTree_->font();
        menuFont.setPointSize(menuFont.pointSize() + 2);
        sectionTree_->setFont(menuFont);
        sectionTree_->setStyleSheet(QStringLiteral(
            "QTreeWidget { padding-top: 12px; }"
            "QTreeWidget::item { padding: 6px 4px; }"
            "QTreeWidget::branch { padding: 0px; }"));
    }

    auto *homeItem = new QTreeWidgetItem(QStringList() << tr("Home"));
    homeItem->setData(0, Qt::UserRole, kPageHome);
    sectionTree_->addTopLevelItem(homeItem);

    auto *homeSeparator = new QTreeWidgetItem(QStringList() << QString());
    homeSeparator->setFlags(Qt::NoItemFlags);
    sectionTree_->addTopLevelItem(homeSeparator);
    {
        auto *separatorLine = new QFrame(sectionTree_);
        separatorLine->setFrameShape(QFrame::HLine);
        separatorLine->setFrameShadow(QFrame::Sunken);
        separatorLine->setStyleSheet(QStringLiteral("color: #3a3a3a;"));
        separatorLine->setMinimumHeight(1);
        sectionTree_->setItemWidget(homeSeparator, 0, separatorLine);
        homeSeparator->setSizeHint(0, QSize(0, 8));
    }

    auto *frigateTemplateItem = new QTreeWidgetItem(QStringList() << tr("Frigates"));
    frigateTemplateItem->setData(0, Qt::UserRole, kPageFrigateTemplate);
    sectionTree_->addTopLevelItem(frigateTemplateItem);

    auto *currenciesItem = new QTreeWidgetItem(QStringList() << tr("Currencies"));
    currenciesItem->setData(0, Qt::UserRole, kPageCurrencies);
    sectionTree_->addTopLevelItem(currenciesItem);

    auto *expeditionItem = new QTreeWidgetItem(QStringList() << tr("Expedition"));
    expeditionItem->setData(0, Qt::UserRole, kPageExpedition);
    sectionTree_->addTopLevelItem(expeditionItem);

    auto *inventoryItem = new QTreeWidgetItem(QStringList() << tr("Inventories"));
    inventoryItem->setData(0, Qt::UserRole, kPageInventory);
    sectionTree_->addTopLevelItem(inventoryItem);

    auto *jsonItem = new QTreeWidgetItem(QStringList() << tr("JSON Explorer"));
    jsonItem->setData(0, Qt::UserRole, kPageJson);
    sectionTree_->addTopLevelItem(jsonItem);

    auto *knownTechItem = new QTreeWidgetItem(QStringList() << tr("Known Technology"));
    knownTechItem->setData(0, Qt::UserRole, kPageKnownTechnology);
    sectionTree_->addTopLevelItem(knownTechItem);

    auto *knownProductItem = new QTreeWidgetItem(QStringList() << tr("Known Products"));
    knownProductItem->setData(0, Qt::UserRole, kPageKnownProduct);
    sectionTree_->addTopLevelItem(knownProductItem);

    auto *settlementItem = new QTreeWidgetItem(QStringList() << tr("Settlement Manager"));
    settlementItem->setData(0, Qt::UserRole, kPageSettlement);
    sectionTree_->addTopLevelItem(settlementItem);

    auto *shipItem = new QTreeWidgetItem(QStringList() << tr("Ship Manager"));
    shipItem->setData(0, Qt::UserRole, kPageShip);
    sectionTree_->addTopLevelItem(shipItem);

    auto *storageItem = new QTreeWidgetItem(QStringList() << tr("Storage Manager"));
    storageItem->setData(0, Qt::UserRole, kPageStorage);
    sectionTree_->addTopLevelItem(storageItem);

    auto *materialLookupItem = new QTreeWidgetItem(QStringList() << tr("Material Lookup"));
    materialLookupItem->setData(0, Qt::UserRole, kPageMaterialLookup);
    sectionTree_->addTopLevelItem(materialLookupItem);

    stackedPages_ = new QStackedWidget(mainSplitter_);

    welcomePage_ = new WelcomePage(this);
    jsonPage_ = new JsonExplorerPage(this);
    inventoryPage_ = new InventoryEditorPage(
        this, InventoryEditorPage::InventorySection::Inventories
                  | InventoryEditorPage::InventorySection::Settlement);
    currenciesPage_ = new InventoryEditorPage(this, InventoryEditorPage::InventorySection::Currencies);
    expeditionPage_ = new InventoryEditorPage(this, InventoryEditorPage::InventorySection::Expedition);
    storageManagerPage_ = new InventoryEditorPage(this, InventoryEditorPage::InventorySection::StorageManager);
    settlementPage_ = new SettlementManagerPage(this);
    shipManagerPage_ = new ShipManagerPage(this);
    frigateManagerPage_ = new FrigateManagerPage(this);
    backupsPage_ = new BackupsPage(this);
    knownTechnologyPage_ = new KnownTechnologyPage(this);
    knownProductPage_ = new KnownProductPage(this);
    backupsPage_->setBackupRoot(backupManager_.rootPath());

    stackedPages_->addWidget(welcomePage_);
    stackedPages_->addWidget(jsonPage_);
    stackedPages_->addWidget(inventoryPage_);
    stackedPages_->addWidget(settlementPage_);
    stackedPages_->addWidget(shipManagerPage_);
    stackedPages_->addWidget(frigateManagerPage_);
    stackedPages_->addWidget(currenciesPage_);
    stackedPages_->addWidget(expeditionPage_);
    stackedPages_->addWidget(storageManagerPage_);
    stackedPages_->addWidget(backupsPage_);
    stackedPages_->addWidget(knownTechnologyPage_);
    stackedPages_->addWidget(knownProductPage_);

    loadingOverlay_ = new LoadingOverlay(this);

    mainSplitter_->addWidget(sectionTree_);
    mainSplitter_->addWidget(stackedPages_);
    mainSplitter_->setStretchFactor(1, 1);

    const int gridWidth = InventoryGridWidget::preferredGridWidth();
    const int gridHeight = InventoryGridWidget::preferredGridHeight(6);
    const int treeWidth = sectionTree_->minimumWidth();
    resize(treeWidth + gridWidth + 60, gridHeight + 200);
    mainSplitter_->setSizes({treeWidth, gridWidth + 60});

    buildMenus();

    statusLabel_ = new QLabel(tr("Ready."), this);
    statusBar()->addWidget(statusLabel_);

    saveWatcher_ = new QFileSystemWatcher(this);
    connect(saveWatcher_, &QFileSystemWatcher::fileChanged, this, &MainWindow::handleSaveFileChanged);

    connect(sectionTree_, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *current, QTreeWidgetItem *previous) {
                if (!current) {
                    return;
                }
                QString key = current->data(0, Qt::UserRole).toString();
                if (stackedPages_->currentWidget() == jsonPage_ && key != kPageJson) {
                    if (!confirmLeaveJsonEditor(current->text(0))) {
                        QSignalBlocker blocker(sectionTree_);
                        sectionTree_->setCurrentItem(previous);
                        return;
                    }
                }

                if (key == kPageHome || key.isEmpty()) {
                    selectPage(kPageHome);
                    return;
                }
                if (key == kPageBackups) {
                    selectPage(kPageBackups);
                    refreshBackupsPage();
                    return;
                }

                if (!ensureSaveLoaded()) {
                    QSignalBlocker blocker(sectionTree_);
                    for (int i = 0; i < sectionTree_->topLevelItemCount(); ++i) {
                        QTreeWidgetItem *item = sectionTree_->topLevelItem(i);
                        if (item && item->data(0, Qt::UserRole).toString() == kPageHome) {
                            sectionTree_->setCurrentItem(item);
                            break;
                        }
                    }
                    return;
                }

                if (key == kPageSettlement) {
                    openSettlementManager();
                } else if (key == kPageShip) {
                    openShipManager();
                } else if (key == kPageFrigateTemplate) {
                    openFrigateTemplateManager();
                } else if (key == kPageJson) {
                    openJsonEditor();
                } else if (key == kPageInventory) {
                    openInventoryEditor();
                } else if (key == kPageCurrencies) {
                    openCurrenciesEditor();
                } else if (key == kPageExpedition) {
                    openExpeditionEditor();
                } else if (key == kPageStorage) {
                    openStorageManager();
                } else if (key == kPageKnownTechnology) {
                    openKnownTechnologyEditor();
                } else if (key == kPageKnownProduct) {
                    openKnownProductEditor();
                } else if (key == kPageMaterialLookup) {
                    openMaterialLookup();
                    if (previous) {
                        QSignalBlocker blocker(sectionTree_);
                        sectionTree_->setCurrentItem(previous);
                    }
                } else {
                    selectPage(key);
                }
            });

    connect(welcomePage_, &WelcomePage::refreshRequested, this, &MainWindow::refreshSaveSlots);
    connect(welcomePage_, &WelcomePage::browseRequested, this, &MainWindow::browseForSave);
    connect(welcomePage_, &WelcomePage::loadSaveRequested, this, &MainWindow::loadSelectedSave);
    connect(welcomePage_, &WelcomePage::openJsonRequested, this, &MainWindow::openJsonEditor);
    connect(welcomePage_, &WelcomePage::openInventoryRequested, this, &MainWindow::openInventoryEditor);
    connect(welcomePage_, &WelcomePage::materialLookupRequested, this, &MainWindow::openMaterialLookup);
    connect(welcomePage_, &WelcomePage::saveChangesRequested, this, &MainWindow::saveChanges);
    connect(welcomePage_, &WelcomePage::syncOtherSaveRequested, this, &MainWindow::syncOtherSave);
    connect(welcomePage_, &WelcomePage::undoSyncRequested, this, &MainWindow::undoSync);

    connect(backupsPage_, &BackupsPage::refreshRequested, this, &MainWindow::refreshBackupsPage);
    connect(backupsPage_, &BackupsPage::openFolderRequested, this, [](const QString &path) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    connect(backupsPage_, &BackupsPage::restoreRequested, this, [this](const BackupEntry &entry) {
        QString targetPath = entry.sourcePath;
        if (targetPath.isEmpty() || !QFileInfo::exists(targetPath)) {
            targetPath = QFileDialog::getSaveFileName(this, tr("Restore Backup To"), QString(),
                                                      tr("No Man's Sky Saves (*.hg);;All Files (*.*)"));
        }
        if (targetPath.isEmpty()) {
            return;
        }
        QString confirm = tr("Restore backup from %1 to:\n%2")
                              .arg(BackupManager::formatTimestamp(entry.backupTimeMs), targetPath);
        if (QMessageBox::question(this, tr("Confirm Restore"), confirm,
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
            != QMessageBox::Yes) {
            return;
        }

        const SaveSlot *slot = findSlotForPath(targetPath);
        QString error;
        if (QFileInfo::exists(targetPath)) {
            backupManager_.createBackup(targetPath, slot, QStringLiteral("pre-restore"), nullptr, &error);
        }
        if (!backupManager_.restoreBackup(entry, targetPath, &error)) {
            setStatus(error.isEmpty() ? tr("Restore failed.") : error);
            return;
        }
        lastBackupMtime_.remove(targetPath);
        setStatus(tr("Backup restored to %1").arg(QFileInfo(targetPath).fileName()));
    });

    connect(jsonPage_, &JsonExplorerPage::statusMessage, this, &MainWindow::setStatus);
    connect(inventoryPage_, &InventoryEditorPage::statusMessage, this, &MainWindow::setStatus);
    connect(currenciesPage_, &InventoryEditorPage::statusMessage, this, &MainWindow::setStatus);
    connect(expeditionPage_, &InventoryEditorPage::statusMessage, this, &MainWindow::setStatus);
    connect(storageManagerPage_, &InventoryEditorPage::statusMessage, this, &MainWindow::setStatus);
    connect(settlementPage_, &SettlementManagerPage::statusMessage, this, &MainWindow::setStatus);
    connect(shipManagerPage_, &ShipManagerPage::statusMessage, this, &MainWindow::setStatus);
    connect(frigateManagerPage_, &FrigateManagerPage::statusMessage, this, &MainWindow::setStatus);
    connect(knownTechnologyPage_, &KnownTechnologyPage::statusMessage, this, &MainWindow::setStatus);
    connect(knownProductPage_, &KnownProductPage::statusMessage, this, &MainWindow::setStatus);

    refreshSaveSlots();
    sectionTree_->setCurrentItem(homeItem);

    (void)QtConcurrent::run([]() {
        ItemCatalog::warmup();
        (void)ItemDefinitionRegistry::allDefinitions();
        (void)LocalizationRegistry::resolveToken(QStringLiteral("UI_PERK_POSITIVE_TITLE"));
    });

    shipManagerPage_->setSizePolicy(settlementPage_->sizePolicy());
}

void MainWindow::buildMenus()
{
    auto *fileMenu = menuBar()->addMenu(tr("File"));
    QAction *openAction = fileMenu->addAction(tr("Open Save..."));
    saveAction_ = fileMenu->addAction(tr("Save Changes"));
    QAction *saveAsAction = fileMenu->addAction(tr("Save As..."));
    QAction *exportAction = fileMenu->addAction(tr("Export JSON..."));
    QAction *backupsAction = fileMenu->addAction(tr("Backups"));
    QAction *materialLookupAction = fileMenu->addAction(tr("Material Lookup..."));
    fileMenu->addSeparator();
    QAction *exitAction = fileMenu->addAction(tr("Exit"));

    auto *viewMenu = menuBar()->addMenu(tr("View"));
    QAction *expandAction = viewMenu->addAction(tr("Expand All"));
    QAction *collapseAction = viewMenu->addAction(tr("Collapse All"));

    auto *inventoryMenu = menuBar()->addMenu(tr("Inventories"));
    QAction *showIdsAction = inventoryMenu->addAction(tr("Show IDs"));
    showIdsAction->setCheckable(true);

    auto *helpMenu = menuBar()->addMenu(tr("Help"));
    QAction *logAction = helpMenu->addAction(tr("Open Log Folder"));
    QAction *aboutAction = helpMenu->addAction(tr("About"));

    connect(openAction, &QAction::triggered, this, &MainWindow::browseForSaveDirectory);
    connect(saveAction_, &QAction::triggered, this, &MainWindow::saveChanges);
    connect(saveAsAction, &QAction::triggered, this, [this]() {
        if (!ensureSaveLoaded()) {
            return;
        }
        QString filePath = QFileDialog::getSaveFileName(this, tr("Save As .hg"), QString(), tr("No Man's Sky Saves (*.hg)"));
        if (filePath.isEmpty()) {
            return;
        }
        QString error;
        if (!jsonPage_->saveAs(filePath, &error)) {
            setStatus(error);
        } else {
            setStatus(tr("Saved %1").arg(QFileInfo(filePath).fileName()));
        }
    });
    connect(exportAction, &QAction::triggered, this, [this]() {
        if (!ensureSaveLoaded()) {
            return;
        }
        QString filePath = QFileDialog::getSaveFileName(this, tr("Export JSON"), QString(), tr("JSON Files (*.json)"));
        if (filePath.isEmpty()) {
            return;
        }
        QString error;
        if (!jsonPage_->exportJson(filePath, &error)) {
            setStatus(error);
        } else {
            setStatus(tr("Exported JSON to %1").arg(QFileInfo(filePath).fileName()));
        }
    });
    connect(backupsAction, &QAction::triggered, this, [this]() {
        selectPage(kPageBackups);
        refreshBackupsPage();
    });
    connect(materialLookupAction, &QAction::triggered, this, &MainWindow::openMaterialLookup);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    connect(expandAction, &QAction::triggered, jsonPage_, &JsonExplorerPage::expandAll);
    connect(collapseAction, &QAction::triggered, jsonPage_, &JsonExplorerPage::collapseAll);

    connect(showIdsAction, &QAction::toggled, this, [this](bool show) {
        if (inventoryPage_) {
            inventoryPage_->setShowIds(show);
        }
        if (currenciesPage_) {
            currenciesPage_->setShowIds(show);
        }
        if (expeditionPage_) {
            expeditionPage_->setShowIds(show);
        }
        if (storageManagerPage_) {
            storageManagerPage_->setShowIds(show);
        }
        const auto grids = findChildren<InventoryGridWidget *>();
        for (auto *grid : grids) {
            grid->setShowIds(show);
        }
    });

    connect(logAction, &QAction::triggered, this, []() {
        const QString logDir = QStringLiteral("~/");
        QDesktopServices::openUrl(QUrl::fromLocalFile(logDir));
    });

    connect(aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::information(this, tr("About Save Explorer"),
                                 tr("Save Explorer - No Man's Sky\nQt-based redesign scaffold."));
    });
}

void MainWindow::refreshSaveSlots()
{
    if (!confirmRefreshUnload()) {
        return;
    }
    unloadCurrentSave();
    saveSlots_ = SaveGameLocator::discoverSaveSlots();
    welcomePage_->setSlots(saveSlots_);
    setStatus(saveSlots_.isEmpty() ? tr("No save slots detected.")
                                   : tr("Found %1 save slot(s).").arg(saveSlots_.size()));
}

void MainWindow::browseForSave()
{
    QString path = QFileDialog::getOpenFileName(this, tr("Select No Man's Sky Save"), QString(),
                                                tr("No Man's Sky Saves (*.hg);;JSON Files (*.json);;All Files (*.*)"));
    if (path.isEmpty()) {
        setStatus(tr("No file selected."));
        return;
    }

    SaveSlot slot;
    slot.latestSave = path;
    slot.slotPath = QFileInfo(path).absolutePath();
    slot.lastModified = QFileInfo(path).lastModified().toMSecsSinceEpoch();
    SaveSlot::SaveFileEntry entry;
    entry.filePath = path;
    entry.lastModified = slot.lastModified;
    slot.saveFiles.append(entry);
    saveSlots_.prepend(slot);
    welcomePage_->setSlots(saveSlots_);
    setStatus(tr("Selected %1").arg(QFileInfo(path).fileName()));
}

void MainWindow::browseForSaveDirectory()
{
    QString path = QFileDialog::getExistingDirectory(this, tr("Select No Man's Sky Save Directory"), QString());
    if (path.isEmpty()) {
        return;
    }

    QList<SaveSlot> newSlots = SaveGameLocator::scanDirectory(path);
    if (newSlots.isEmpty()) {
        // If no slots found in subdirectories, try scanning the directory itself as a slot
        // or just warn the user.
        QMessageBox::information(this, tr("No Saves Found"), tr("No save files were found in the selected directory."));
        return;
    }

    saveSlots_ = newSlots;
    welcomePage_->setSlots(saveSlots_);
    selectPage(kPageHome);
    setStatus(tr("Loaded %1 save slot(s) from directory.").arg(saveSlots_.size()));
}

void MainWindow::loadSelectedSave()
{
    QString path = welcomePage_->selectedSavePath();
    loadSavePath(path);
}

void MainWindow::loadSavePath(const QString &path)
{
    if (path.isEmpty()) {
        setStatus(tr("Choose a save file first."));
        return;
    }

    currentSaveFile_ = path;
    maybeBackupOnLoad(currentSaveFile_);
    updateSaveWatcher(currentSaveFile_);
    welcomePage_->setSaveEnabled(false);
    welcomePage_->setLoadedSavePath(currentSaveFile_);
    setStatus(tr("Loaded %1").arg(QFileInfo(path).fileName()));
}

void MainWindow::maybeBackupOnLoad(const QString &path)
{
    QFileInfo info(path);
    if (!info.exists()) {
        return;
    }
    qint64 mtime = info.lastModified().toMSecsSinceEpoch();
    if (lastBackupMtime_.value(path) == mtime) {
        return;
    }
    const SaveSlot *slot = findSlotForPath(path);
    QString error;
    if (backupManager_.createBackup(path, slot, QStringLiteral("load"), nullptr, &error)) {
        lastBackupMtime_.insert(path, mtime);
    } else if (!error.isEmpty()) {
        qWarning() << "Backup failed:" << error;
    }
}

void MainWindow::refreshBackupsPage()
{
    if (!backupsPage_) {
        return;
    }
    backupsPage_->setBackupRoot(backupManager_.rootPath());
    QString error;
    QList<BackupEntry> entries = backupManager_.listBackups(&error);
    if (backupsPage_->currentOnlyEnabled() && !currentSaveFile_.isEmpty()) {
        QString target = QFileInfo(currentSaveFile_).canonicalFilePath();
        if (target.isEmpty()) {
            target = QFileInfo(currentSaveFile_).absoluteFilePath();
        }
        QList<BackupEntry> filtered;
        filtered.reserve(entries.size());
        for (const BackupEntry &entry : entries) {
            QString source = QFileInfo(entry.sourcePath).canonicalFilePath();
            if (source.isEmpty()) {
                source = QFileInfo(entry.sourcePath).absoluteFilePath();
            }
            if (!source.isEmpty() && source == target) {
                filtered.append(entry);
            }
        }
        entries = filtered;
    }
    backupsPage_->setBackups(entries);
    if (!error.isEmpty()) {
        qWarning() << "Backup listing error:" << error;
    }
}

const SaveSlot *MainWindow::findSlotForPath(const QString &path) const
{
    QString target = QFileInfo(path).canonicalFilePath();
    if (target.isEmpty()) {
        target = QFileInfo(path).absoluteFilePath();
    }
    for (const SaveSlot &slot : saveSlots_) {
        for (const SaveSlot::SaveFileEntry &entry : slot.saveFiles) {
            QString candidate = QFileInfo(entry.filePath).canonicalFilePath();
            if (candidate.isEmpty()) {
                candidate = QFileInfo(entry.filePath).absoluteFilePath();
            }
            if (!candidate.isEmpty() && candidate == target) {
                return &slot;
            }
        }
    }
    return nullptr;
}

void MainWindow::loadSaveInBackground(
    const QString &path, const QString &statusText,
    const std::function<void(const LoadResult &)> &onLoaded)
{
    if (path.isEmpty() || loadingWatcher_.isRunning()) {
        return;
    }

    loadingOverlay_->showMessage(statusText);
    auto loadTask = [path]() {
        LoadResult result;
        QByteArray content;
        if (!SaveCache::loadWithLossless(path, &content, &result.doc, &result.lossless, &result.error)) {
            return result;
        }
        return result;
    };

    connect(&loadingWatcher_, &QFutureWatcher<LoadResult>::finished, this,
            [this, onLoaded]() {
                loadingOverlay_->hide();
                LoadResult result = loadingWatcher_.result();
                if (!result.error.isEmpty() || result.doc.isNull() || !result.lossless) {
                    setStatus(result.error.isEmpty() ? tr("Failed to load save data.") : result.error);
                    return;
                }
                onLoaded(result);
            },
            Qt::SingleShotConnection);

    loadingWatcher_.setFuture(QtConcurrent::run(loadTask));
}

void MainWindow::openJsonEditor()
{
    if (!ensureSaveLoaded()) {
        return;
    }
    if (jsonPage_->hasLoadedSave() && jsonPage_->currentFilePath() == currentSaveFile_) {
        selectPage(kPageJson);
        return;
    }

    const QString path = currentSaveFile_;
    loadSaveInBackground(path, tr("Decoding save file, please wait..."), [this, path](const LoadResult &result) {
        jsonPage_->setRootDoc(result.doc, path, result.lossless);
        currentSaveFile_ = path;
        welcomePage_->setSaveEnabled(jsonPage_->hasUnsavedChanges());
        welcomePage_->setLoadedSavePath(currentSaveFile_);
        updateSaveWatcher(currentSaveFile_);
        selectPage(kPageJson);
    });
}

void MainWindow::openInventoryEditor()
{
    if (!confirmLeaveJsonEditor(tr("Inventories"))) {
        return;
    }
    if (!ensureSaveLoaded()) {
        return;
    }
    if (inventoryPage_->hasLoadedSave() && inventoryPage_->currentFilePath() == currentSaveFile_) {
        selectPage(kPageInventory);
        return;
    }
    if (inventoryPage_->hasLoadedSave() && inventoryPage_->hasUnsavedChanges()) {
        if (!confirmDiscardOrSave(tr("Inventories"), [this](QString *error) {
                return inventoryPage_->saveChanges(error);
            })) {
            return;
        }
    }
    QString path = currentSaveFile_;

    loadSaveInBackground(path, tr("Loading inventories..."), [this, path](const LoadResult &result) {
        QString error;
        if (!inventoryPage_->loadFromPrepared(path, result.doc, result.lossless, &error)) {
            setStatus(error.isEmpty() ? tr("Failed to load Inventories.") : error);
            return;
        }
        currentSaveFile_ = path;
        updateSaveWatcher(currentSaveFile_);
        welcomePage_->setSaveEnabled(inventoryPage_->hasUnsavedChanges());
        welcomePage_->setLoadedSavePath(currentSaveFile_);
        selectPage(kPageInventory);
    });
}

void MainWindow::openCurrenciesEditor()
{
    if (!confirmLeaveJsonEditor(tr("Currencies"))) {
        return;
    }
    if (!ensureSaveLoaded()) {
        return;
    }
    if (currenciesPage_->hasLoadedSave() && currenciesPage_->currentFilePath() == currentSaveFile_) {
        selectPage(kPageCurrencies);
        return;
    }
    if (currenciesPage_->hasLoadedSave() && currenciesPage_->hasUnsavedChanges()) {
        if (!confirmDiscardOrSave(tr("Currencies"), [this](QString *error) {
                return currenciesPage_->saveChanges(error);
            })) {
            return;
        }
    }
    QString path = currentSaveFile_;

    loadSaveInBackground(path, tr("Loading currencies..."), [this, path](const LoadResult &result) {
        QString error;
        if (!currenciesPage_->loadFromPrepared(path, result.doc, result.lossless, &error)) {
            setStatus(error.isEmpty() ? tr("Failed to load Currencies.") : error);
            return;
        }
        currentSaveFile_ = path;
        updateSaveWatcher(currentSaveFile_);
        welcomePage_->setSaveEnabled(currenciesPage_->hasUnsavedChanges());
        welcomePage_->setLoadedSavePath(currentSaveFile_);
        selectPage(kPageCurrencies);
    });
}

void MainWindow::openExpeditionEditor()
{
    if (!confirmLeaveJsonEditor(tr("Expedition"))) {
        return;
    }
    if (!ensureSaveLoaded()) {
        return;
    }
    if (expeditionPage_->hasLoadedSave() && expeditionPage_->currentFilePath() == currentSaveFile_) {
        selectPage(kPageExpedition);
        return;
    }
    if (expeditionPage_->hasLoadedSave() && expeditionPage_->hasUnsavedChanges()) {
        if (!confirmDiscardOrSave(tr("Expedition"), [this](QString *error) {
                return expeditionPage_->saveChanges(error);
            })) {
            return;
        }
    }
    QString path = currentSaveFile_;

    loadSaveInBackground(path, tr("Loading expedition data..."), [this, path](const LoadResult &result) {
        QString error;
        if (!expeditionPage_->loadFromPrepared(path, result.doc, result.lossless, &error)) {
            setStatus(error.isEmpty() ? tr("Failed to load Expedition.") : error);
            return;
        }
        currentSaveFile_ = path;
        updateSaveWatcher(currentSaveFile_);
        welcomePage_->setSaveEnabled(expeditionPage_->hasUnsavedChanges());
        welcomePage_->setLoadedSavePath(currentSaveFile_);
        selectPage(kPageExpedition);
    });
}

void MainWindow::openStorageManager()
{
    if (!confirmLeaveJsonEditor(tr("Storage Manager"))) {
        return;
    }
    if (!ensureSaveLoaded()) {
        return;
    }
    if (storageManagerPage_->hasLoadedSave()
        && storageManagerPage_->currentFilePath() == currentSaveFile_) {
        selectPage(kPageStorage);
        return;
    }
    if (storageManagerPage_->hasLoadedSave() && storageManagerPage_->hasUnsavedChanges()) {
        if (!confirmDiscardOrSave(tr("Storage Manager"), [this](QString *error) {
                return storageManagerPage_->saveChanges(error);
            })) {
            return;
        }
    }
    QString path = currentSaveFile_;

    loadSaveInBackground(path, tr("Loading storage manager..."), [this, path](const LoadResult &result) {
        QString error;
        if (!storageManagerPage_->loadFromPrepared(path, result.doc, result.lossless, &error)) {
            setStatus(error.isEmpty() ? tr("Failed to load Storage Manager.") : error);
            return;
        }
        currentSaveFile_ = path;
        updateSaveWatcher(currentSaveFile_);
        welcomePage_->setSaveEnabled(storageManagerPage_->hasUnsavedChanges());
        welcomePage_->setLoadedSavePath(currentSaveFile_);
        selectPage(kPageStorage);
    });
}

void MainWindow::openSettlementManager()
{
    if (!confirmLeaveJsonEditor(tr("Settlement Manager"))) {
        return;
    }
    if (!ensureSaveLoaded()) {
        return;
    }
    QString path = currentSaveFile_;
    if (settlementPage_->hasLoadedSave()
        && settlementPage_->currentFilePath() == currentSaveFile_) {
        selectPage(kPageSettlement);
        return;
    }
    if (settlementPage_->hasLoadedSave() && settlementPage_->hasUnsavedChanges()) {
        if (!confirmDiscardOrSave(tr("Settlement Manager"), [this](QString *error) {
                return settlementPage_->saveChanges(error);
            })) {
            return;
        }
    }
    if (path.isEmpty()) {
        path = QFileDialog::getOpenFileName(this, tr("Select No Man's Sky Save"), QString(),
                                            tr("No Man's Sky Saves (*.hg);;JSON Files (*.json);;All Files (*.*)"));
    }
    if (path.isEmpty()) {
        setStatus(tr("Choose a save slot first."));
        return;
    }

    loadSaveInBackground(path, tr("Loading settlement manager..."), [this, path](const LoadResult &result) {
        QString error;
        if (!settlementPage_->loadFromPrepared(path, result.doc, result.lossless, &error)) {
            setStatus(error.isEmpty() ? tr("Failed to load Settlement Manager.") : error);
            return;
        }
        currentSaveFile_ = path;
        maybeBackupOnLoad(currentSaveFile_);
        updateSaveWatcher(currentSaveFile_);
        welcomePage_->setSaveEnabled(settlementPage_->hasUnsavedChanges());
        welcomePage_->setLoadedSavePath(currentSaveFile_);
        selectPage(kPageSettlement);
    });
}

void MainWindow::openShipManager()
{
    if (!confirmLeaveJsonEditor(tr("Ship Manager"))) {
        return;
    }
    if (!ensureSaveLoaded()) {
        return;
    }
    QString path = currentSaveFile_;

    if (shipManagerPage_->hasLoadedSave()
        && shipManagerPage_->currentFilePath() == currentSaveFile_) {
        selectPage(kPageShip);
        return;
    }
    if (shipManagerPage_->hasLoadedSave() && shipManagerPage_->hasUnsavedChanges()) {
        if (!confirmDiscardOrSave(tr("Ship Manager"), [this](QString *error) {
                return shipManagerPage_->saveChanges(error);
            })) {
            return;
        }
    }

    loadSaveInBackground(path, tr("Loading ship manager..."), [this, path](const LoadResult &result) {
        QString error;
        if (!shipManagerPage_->loadFromPrepared(path, result.doc, result.lossless, &error)) {
            setStatus(error.isEmpty() ? tr("Failed to load Ship Manager.") : error);
            return;
        }
        currentSaveFile_ = path;
        updateSaveWatcher(currentSaveFile_);
        welcomePage_->setSaveEnabled(shipManagerPage_->hasUnsavedChanges());
        welcomePage_->setLoadedSavePath(currentSaveFile_);
        selectPage(kPageShip);
    });
}

void MainWindow::openFrigateTemplateManager()
{
    if (!confirmLeaveJsonEditor(tr("Frigates"))) {
        return;
    }
    if (!ensureSaveLoaded()) {
        return;
    }
    QString path = currentSaveFile_;

    if (frigateManagerPage_->hasLoadedSave()
        && frigateManagerPage_->currentFilePath() == currentSaveFile_) {
        selectPage(kPageFrigateTemplate);
        return;
    }
    if (frigateManagerPage_->hasLoadedSave() && frigateManagerPage_->hasUnsavedChanges()) {
        if (!confirmDiscardOrSave(tr("Frigates"), [this](QString *error) {
                return frigateManagerPage_->saveChanges(error);
            })) {
            return;
        }
    }

    loadSaveInBackground(path, tr("Loading frigates..."), [this, path](const LoadResult &result) {
        QString error;
        if (!frigateManagerPage_->loadFromPrepared(path, result.doc, result.lossless, &error)) {
            setStatus(error.isEmpty() ? tr("Failed to load Frigates.") : error);
            return;
        }
        currentSaveFile_ = path;
        updateSaveWatcher(currentSaveFile_);
        welcomePage_->setSaveEnabled(frigateManagerPage_->hasUnsavedChanges());
        welcomePage_->setLoadedSavePath(currentSaveFile_);
        selectPage(kPageFrigateTemplate);
    });
}

void MainWindow::openMaterialLookup()
{
    MaterialLookupDialog dialog(this);
    dialog.exec();
}

void MainWindow::openKnownTechnologyEditor()
{
    if (!confirmLeaveJsonEditor(tr("Known Technology"))) {
        return;
    }
    if (!ensureSaveLoaded()) {
        return;
    }
    QString path = currentSaveFile_;

    if (knownTechnologyPage_->hasLoadedSave()
        && knownTechnologyPage_->currentFilePath() == path) {
        selectPage(kPageKnownTechnology);
        return;
    }
    if (knownTechnologyPage_->hasLoadedSave() && knownTechnologyPage_->hasUnsavedChanges()) {
        if (!confirmDiscardOrSave(tr("Known Technology"), [this](QString *error) {
                return knownTechnologyPage_->saveChanges(error);
            })) {
            return;
        }
    }

    loadSaveInBackground(path, tr("Loading known technology..."), [this, path](const LoadResult &result) {
        QString error;
        if (!knownTechnologyPage_->loadFromPrepared(path, result.doc, result.lossless, &error)) {
            setStatus(error.isEmpty() ? tr("Failed to load Known Technology.") : error);
            return;
        }
        currentSaveFile_ = path;
        updateSaveWatcher(currentSaveFile_);
        welcomePage_->setSaveEnabled(knownTechnologyPage_->hasUnsavedChanges());
        welcomePage_->setLoadedSavePath(currentSaveFile_);
        selectPage(kPageKnownTechnology);
    });
}

void MainWindow::openKnownProductEditor()
{
    if (!confirmLeaveJsonEditor(tr("Known Products"))) {
        return;
    }
    if (!ensureSaveLoaded()) {
        return;
    }
    QString path = currentSaveFile_;

    if (knownProductPage_->hasLoadedSave()
        && knownProductPage_->currentFilePath() == path) {
        selectPage(kPageKnownProduct);
        return;
    }
    if (knownProductPage_->hasLoadedSave() && knownProductPage_->hasUnsavedChanges()) {
        if (!confirmDiscardOrSave(tr("Known Products"), [this](QString *error) {
                return knownProductPage_->saveChanges(error);
            })) {
            return;
        }
    }

    loadSaveInBackground(path, tr("Loading known products..."), [this, path](const LoadResult &result) {
        QString error;
        if (!knownProductPage_->loadFromPrepared(path, result.doc, result.lossless, &error)) {
            setStatus(error.isEmpty() ? tr("Failed to load Known Products.") : error);
            return;
        }
        currentSaveFile_ = path;
        updateSaveWatcher(currentSaveFile_);
        welcomePage_->setSaveEnabled(knownProductPage_->hasUnsavedChanges());
        welcomePage_->setLoadedSavePath(currentSaveFile_);
        selectPage(kPageKnownProduct);
    });
}

void MainWindow::saveChanges()
{
    if (stackedPages_->currentWidget() == welcomePage_ && syncPending_) {
        for (const PendingSyncTarget &target : pendingSync_.targets) {
            QFile targetFile(target.path);
            if (!targetFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                setStatus(tr("Unable to write %1").arg(target.path));
                return;
            }
            if (target.path == currentSaveFile_) {
                ignoreNextFileChange_ = true;
            }
            if (targetFile.write(pendingSync_.sourceBytes) != pendingSync_.sourceBytes.size()) {
                setStatus(tr("Failed to write %1").arg(target.path));
                return;
            }
            targetFile.close();
        }
        syncPending_ = false;
        syncUndoAvailable_ = true;
        welcomePage_->setSyncState(syncPending_, syncUndoAvailable_);
        setStatus(tr("Sync saved to %1 file(s).").arg(pendingSync_.targets.size()));
        return;
    }

    if (!ensureSaveLoaded()) {
        return;
    }

    QWidget *activePage = stackedPages_->currentWidget();
    QString error;
    bool saved = false;

    if (activePage == jsonPage_ && jsonPage_->hasLoadedSave()) {
        saved = jsonPage_->saveChanges(&error);
    } else if (activePage == inventoryPage_ && inventoryPage_->hasLoadedSave()) {
        saved = inventoryPage_->saveChanges(&error);
    } else if (activePage == settlementPage_ && settlementPage_->hasLoadedSave()) {
        saved = settlementPage_->saveChanges(&error);
    } else if (activePage == shipManagerPage_ && shipManagerPage_->hasLoadedSave()) {
        saved = shipManagerPage_->saveChanges(&error);
    } else if (activePage == frigateManagerPage_ && frigateManagerPage_->hasLoadedSave()) {
        saved = frigateManagerPage_->saveChanges(&error);
    } else if (activePage == currenciesPage_ && currenciesPage_->hasLoadedSave()) {
        saved = currenciesPage_->saveChanges(&error);
    } else if (activePage == expeditionPage_ && expeditionPage_->hasLoadedSave()) {
        saved = expeditionPage_->saveChanges(&error);
    } else if (activePage == storageManagerPage_ && storageManagerPage_->hasLoadedSave()) {
        saved = storageManagerPage_->saveChanges(&error);
    } else if (activePage == knownTechnologyPage_ && knownTechnologyPage_->hasLoadedSave()) {
        saved = knownTechnologyPage_->saveChanges(&error);
    } else if (activePage == knownProductPage_ && knownProductPage_->hasLoadedSave()) {
        saved = knownProductPage_->saveChanges(&error);
    } else if (jsonPage_->hasLoadedSave()) {
        saved = jsonPage_->saveChanges(&error);
    } else if (inventoryPage_->hasLoadedSave()) {
        saved = inventoryPage_->saveChanges(&error);
    } else if (currenciesPage_->hasLoadedSave()) {
        saved = currenciesPage_->saveChanges(&error);
    } else if (expeditionPage_->hasLoadedSave()) {
        saved = expeditionPage_->saveChanges(&error);
    } else if (storageManagerPage_->hasLoadedSave()) {
        saved = storageManagerPage_->saveChanges(&error);
    } else if (knownTechnologyPage_->hasLoadedSave()) {
        saved = knownTechnologyPage_->saveChanges(&error);
    } else if (knownProductPage_->hasLoadedSave()) {
        saved = knownProductPage_->saveChanges(&error);
    } else if (settlementPage_->hasLoadedSave()) {
        saved = settlementPage_->saveChanges(&error);
    } else if (shipManagerPage_->hasLoadedSave()) {
        saved = shipManagerPage_->saveChanges(&error);
    }

    if (!saved) {
        setStatus(error.isEmpty() ? tr("No active editor to save.") : error);
        return;
    }
    ignoreNextFileChange_ = true;
    SaveCache::clear();
    updateHomeSaveEnabled();
    setStatus(tr("Saved changes."));
}

void MainWindow::syncOtherSave()
{
    SaveSlot slot = welcomePage_->selectedSlot();
    if (slot.saveFiles.size() < 2) {
        setStatus(tr("No other save file found in this slot."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Sync Saves"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *label = new QLabel(tr("Select the save that other files should sync to:"), &dialog);
    layout->addWidget(label);

    auto *list = new QListWidget(&dialog);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(list);

    QList<SaveSlot::SaveFileEntry> entries = slot.saveFiles;
    for (const SaveSlot::SaveFileEntry &entry : entries) {
        list->addItem(QFileInfo(entry.filePath).fileName());
    }

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    QPushButton *syncButton = new QPushButton(tr("Sync"), &dialog);
    syncButton->setEnabled(false);
    buttonBox->addButton(syncButton, QDialogButtonBox::AcceptRole);
    layout->addWidget(buttonBox);

    connect(list, &QListWidget::currentRowChanged, &dialog, [syncButton](int row) {
        syncButton->setEnabled(row >= 0);
    });
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(syncButton, &QPushButton::clicked, &dialog, &QDialog::accept);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    int selectedRow = list->currentRow();
    if (selectedRow < 0 || selectedRow >= entries.size()) {
        return;
    }

    QString sourcePath = entries.at(selectedRow).filePath;
    QStringList targetNames;
    QList<SaveSlot::SaveFileEntry> targets;
    for (int i = 0; i < entries.size(); ++i) {
        if (i == selectedRow) {
            continue;
        }
        targets.append(entries.at(i));
        targetNames.append(QFileInfo(entries.at(i).filePath).fileName());
    }

    QString confirmText;
    if (targets.size() == 1) {
        confirmText = tr("Sync %1 with %2?\n%2 will be overwritten.")
                          .arg(QFileInfo(sourcePath).fileName(),
                               targetNames.first());
    } else {
        confirmText = tr("Sync %1 with %2?\nThese files will be overwritten.")
                          .arg(QFileInfo(sourcePath).fileName(),
                               targetNames.join(tr(", ")));
    }

    if (QMessageBox::question(this, tr("Confirm Sync"), confirmText,
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes) {
        return;
    }

    QFile sourceFile(sourcePath);
    if (!sourceFile.open(QIODevice::ReadOnly)) {
        setStatus(tr("Unable to read %1").arg(sourcePath));
        return;
    }
    QByteArray sourceBytes = sourceFile.readAll();
    sourceFile.close();
    if (sourceBytes.isEmpty()) {
        setStatus(tr("Selected save is empty."));
        return;
    }

    QList<PendingSyncTarget> pendingTargets;
    for (const SaveSlot::SaveFileEntry &entry : targets) {
        QFile targetFile(entry.filePath);
        if (!targetFile.open(QIODevice::ReadOnly)) {
            setStatus(tr("Unable to read %1").arg(entry.filePath));
            return;
        }
        PendingSyncTarget target;
        target.path = entry.filePath;
        target.originalBytes = targetFile.readAll();
        targetFile.close();
        pendingTargets.append(target);
    }

    pendingSync_.sourcePath = sourcePath;
    pendingSync_.sourceBytes = sourceBytes;
    pendingSync_.targets = pendingTargets;
    syncPending_ = true;
    syncUndoAvailable_ = false;
    welcomePage_->setSyncState(syncPending_, syncUndoAvailable_);

    setStatus(tr("Sync staged from %1. Save Changes to apply.")
                  .arg(QFileInfo(sourcePath).fileName()));
}

void MainWindow::undoSync()
{
    if (syncPending_) {
        syncPending_ = false;
        pendingSync_ = PendingSync();
        welcomePage_->setSyncState(syncPending_, syncUndoAvailable_);
        setStatus(tr("Staged sync discarded."));
        return;
    }

    if (!syncUndoAvailable_) {
        setStatus(tr("No sync to undo."));
        return;
    }

    for (const PendingSyncTarget &target : pendingSync_.targets) {
        QFile targetFile(target.path);
        if (!targetFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            setStatus(tr("Unable to write %1").arg(target.path));
            return;
        }
        if (target.path == currentSaveFile_) {
            ignoreNextFileChange_ = true;
        }
        if (targetFile.write(target.originalBytes) != target.originalBytes.size()) {
            setStatus(tr("Failed to restore %1").arg(target.path));
            return;
        }
        targetFile.close();
    }

    syncUndoAvailable_ = false;
    pendingSync_ = PendingSync();
    welcomePage_->setSyncState(syncPending_, syncUndoAvailable_);
    setStatus(tr("Sync undone."));
}

void MainWindow::setStatus(const QString &text)
{
    qInfo() << "Status bar:" << text;
    statusLabel_->setText(text);
}

bool MainWindow::confirmRefreshUnload()
{
    if (!hasPendingChanges() && !syncPending_) {
        return true;
    }

    QMessageBox::StandardButton response = QMessageBox::warning(
        this,
        tr("Unsaved Changes"),
        tr("Refreshing will unload the current save and discard pending changes.\n"
           "Do you want to continue?"),
        QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Cancel);
    return response == QMessageBox::Discard;
}

void MainWindow::unloadCurrentSave()
{
    if (currentSaveFile_.isEmpty()) {
        return;
    }

    SaveCache::clear();
    currentSaveFile_.clear();
    updateSaveWatcher(QString());
    if (jsonPage_) {
        jsonPage_->clearLoadedSave();
    }
    if (inventoryPage_) {
        inventoryPage_->clearLoadedSave();
    }
    if (currenciesPage_) {
        currenciesPage_->clearLoadedSave();
    }
    if (expeditionPage_) {
        expeditionPage_->clearLoadedSave();
    }
    if (storageManagerPage_) {
        storageManagerPage_->clearLoadedSave();
    }
    if (settlementPage_) {
        settlementPage_->clearLoadedSave();
    }
    if (shipManagerPage_) {
        shipManagerPage_->clearLoadedSave();
    }
    if (frigateManagerPage_) {
        frigateManagerPage_->clearLoadedSave();
    }
    if (knownTechnologyPage_) {
        knownTechnologyPage_->clearLoadedSave();
    }
    if (knownProductPage_) {
        knownProductPage_->clearLoadedSave();
    }
    welcomePage_->setSaveEnabled(false);
    welcomePage_->setLoadedSavePath(QString());
    syncPending_ = false;
    syncUndoAvailable_ = false;
    pendingSync_ = PendingSync();
    welcomePage_->setSyncState(syncPending_, syncUndoAvailable_);
    selectPage(kPageHome);
}

bool MainWindow::ensureSaveLoaded()
{
    if (currentSaveFile_.isEmpty()) {
        QMessageBox::information(this, tr("No Save Loaded"),
                                 tr("Please load a save file first."));
        return false;
    }
    return true;
}

void MainWindow::selectPage(const QString &key)
{
    if (key == kPageHome) {
        updateHomeSaveEnabled();
        stackedPages_->setCurrentIndex(0);
    } else if (key == kPageJson) {
        stackedPages_->setCurrentIndex(1);
    } else if (key == kPageInventory) {
        stackedPages_->setCurrentIndex(2);
    } else if (key == kPageSettlement) {
        stackedPages_->setCurrentIndex(3);
    } else if (key == kPageShip) {
        stackedPages_->setCurrentIndex(4);
    } else if (key == kPageCurrencies) {
        stackedPages_->setCurrentIndex(6);
    } else if (key == kPageExpedition) {
        stackedPages_->setCurrentIndex(7);
    } else if (key == kPageStorage) {
        stackedPages_->setCurrentIndex(8);
    } else if (key == kPageBackups) {
        stackedPages_->setCurrentIndex(9);
    } else if (key == kPageFrigateTemplate) {
        stackedPages_->setCurrentIndex(5);
    } else if (key == kPageKnownTechnology) {
        stackedPages_->setCurrentIndex(10);
    } else if (key == kPageKnownProduct) {
        stackedPages_->setCurrentIndex(11);
    }
}

bool MainWindow::hasPendingChanges() const
{
    return (jsonPage_ && jsonPage_->hasLoadedSave() && jsonPage_->hasUnsavedChanges())
        || (inventoryPage_ && inventoryPage_->hasLoadedSave() && inventoryPage_->hasUnsavedChanges())
        || (currenciesPage_ && currenciesPage_->hasLoadedSave() && currenciesPage_->hasUnsavedChanges())
        || (expeditionPage_ && expeditionPage_->hasLoadedSave() && expeditionPage_->hasUnsavedChanges())
        || (storageManagerPage_ && storageManagerPage_->hasLoadedSave() && storageManagerPage_->hasUnsavedChanges())
        || (knownTechnologyPage_ && knownTechnologyPage_->hasLoadedSave() && knownTechnologyPage_->hasUnsavedChanges())
        || (knownProductPage_ && knownProductPage_->hasLoadedSave() && knownProductPage_->hasUnsavedChanges())
        || (settlementPage_ && settlementPage_->hasLoadedSave() && settlementPage_->hasUnsavedChanges())
        || (shipManagerPage_ && shipManagerPage_->hasLoadedSave() && shipManagerPage_->hasUnsavedChanges())
        || (frigateManagerPage_ && frigateManagerPage_->hasLoadedSave() && frigateManagerPage_->hasUnsavedChanges());
}

void MainWindow::updateHomeSaveEnabled()
{
    bool pending = hasPendingChanges();
    if (welcomePage_) {
        welcomePage_->setSaveEnabled(pending);
    }
    if (saveAction_) {
        saveAction_->setEnabled(pending || syncPending_);
    }
}


QString MainWindow::resolveLatestSavePath(const SaveSlot &slot) const
{
    if (!slot.latestSave.isEmpty()) {
        return slot.latestSave;
    }
    return QString();
}

bool MainWindow::confirmLeaveJsonEditor(const QString &nextAction)
{
    if (stackedPages_->currentWidget() != jsonPage_) {
        return true;
    }
    if (!jsonPage_->hasLoadedSave() || !jsonPage_->hasUnsavedChanges()) {
        return true;
    }
    return confirmDiscardOrSave(tr("JSON Explorer"), [this](QString *error) {
        if (!jsonPage_->saveChanges(error)) {
            return false;
        }
        ignoreNextFileChange_ = true;
        SaveCache::clear();
        updateHomeSaveEnabled();
        setStatus(tr("Saved changes."));
        return true;
    });
}

bool MainWindow::confirmDiscardOrSave(const QString &pageName,
                                      const std::function<bool(QString *)> &saveFn)
{
    QMessageBox::StandardButton response = QMessageBox::warning(
        this,
        tr("Unsaved Changes"),
        tr("You have unsaved changes in %1.\n"
           "Do you want to save them before continuing?").arg(pageName),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (response == QMessageBox::Cancel) {
        return false;
    }
    if (response == QMessageBox::Discard) {
        return true;
    }
    QString error;
    if (!saveFn(&error)) {
        setStatus(error.isEmpty() ? tr("Failed to save changes.") : error);
        return false;
    }
    return true;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    struct PendingChange {
        QString name;
        std::function<bool(QString *)> saveFn;
    };

    QList<PendingChange> pending;
    if (jsonPage_->hasLoadedSave() && jsonPage_->hasUnsavedChanges()) {
        pending.append({tr("JSON Explorer"), [this](QString *error) {
            return jsonPage_->saveChanges(error);
        }});
    }
    if (inventoryPage_->hasLoadedSave() && inventoryPage_->hasUnsavedChanges()) {
        pending.append({tr("Inventories"), [this](QString *error) {
            return inventoryPage_->saveChanges(error);
        }});
    }
    if (currenciesPage_->hasLoadedSave() && currenciesPage_->hasUnsavedChanges()) {
        pending.append({tr("Currencies"), [this](QString *error) {
            return currenciesPage_->saveChanges(error);
        }});
    }
    if (expeditionPage_->hasLoadedSave() && expeditionPage_->hasUnsavedChanges()) {
        pending.append({tr("Expedition"), [this](QString *error) {
            return expeditionPage_->saveChanges(error);
        }});
    }
    if (storageManagerPage_->hasLoadedSave() && storageManagerPage_->hasUnsavedChanges()) {
        pending.append({tr("Storage Manager"), [this](QString *error) {
            return storageManagerPage_->saveChanges(error);
        }});
    }
    if (knownTechnologyPage_->hasLoadedSave() && knownTechnologyPage_->hasUnsavedChanges()) {
        pending.append({tr("Known Technology"), [this](QString *error) {
            return knownTechnologyPage_->saveChanges(error);
        }});
    }
    if (knownProductPage_->hasLoadedSave() && knownProductPage_->hasUnsavedChanges()) {
        pending.append({tr("Known Products"), [this](QString *error) {
            return knownProductPage_->saveChanges(error);
        }});
    }
    if (settlementPage_->hasLoadedSave() && settlementPage_->hasUnsavedChanges()) {
        pending.append({tr("Settlement Manager"), [this](QString *error) {
            return settlementPage_->saveChanges(error);
        }});
    }
    if (shipManagerPage_->hasLoadedSave() && shipManagerPage_->hasUnsavedChanges()) {
        pending.append({tr("Ship Manager"), [this](QString *error) {
            return shipManagerPage_->saveChanges(error);
        }});
    }
    if (frigateManagerPage_->hasLoadedSave() && frigateManagerPage_->hasUnsavedChanges()) {
        pending.append({tr("Frigates"), [this](QString *error) {
            return frigateManagerPage_->saveChanges(error);
        }});
    }

    if (pending.isEmpty()) {
        event->accept();
        return;
    }

    QStringList sections;
    for (const auto &entry : pending) {
        sections.append(entry.name);
    }

    QMessageBox::StandardButton response = QMessageBox::warning(
        this,
        tr("Unsaved Changes"),
        tr("You have unsaved changes in: %1.\n"
           "Do you want to save them before closing?")
            .arg(sections.join(tr(", "))),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);

    if (response == QMessageBox::Cancel) {
        event->ignore();
        return;
    }
    if (response == QMessageBox::Discard) {
        event->accept();
        return;
    }

    for (const auto &entry : pending) {
        QString error;
        if (!entry.saveFn(&error)) {
            setStatus(error.isEmpty() ? tr("Failed to save changes.") : error);
            event->ignore();
            return;
        }
    }
    event->accept();
}

void MainWindow::updateSaveWatcher(const QString &path)
{
    if (!saveWatcher_) {
        return;
    }
    const QStringList watched = saveWatcher_->files();
    if (!watched.isEmpty()) {
        saveWatcher_->removePaths(watched);
    }
    if (!path.isEmpty()) {
        saveWatcher_->addPath(path);
    }
}

void MainWindow::handleSaveFileChanged(const QString &path)
{
    if (ignoreNextFileChange_) {
        ignoreNextFileChange_ = false;
        updateSaveWatcher(path);
        return;
    }
    if (path.isEmpty() || path != currentSaveFile_ || !jsonPage_->hasLoadedSave()) {
        updateSaveWatcher(path);
        return;
    }

    QMessageBox::StandardButton response = QMessageBox::question(
        this,
        tr("Save File Changed"),
        tr("The save file was modified by another process.\nReload it now?"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::Yes);
    if (response == QMessageBox::Yes) {
        QString error;
        if (!jsonPage_->loadFromFile(currentSaveFile_, &error)) {
            setStatus(error);
        } else {
            setStatus(tr("Reloaded %1").arg(QFileInfo(currentSaveFile_).fileName()));
        }
    }
    updateSaveWatcher(path);
}
