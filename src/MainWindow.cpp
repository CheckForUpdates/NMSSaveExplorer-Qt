#include "MainWindow.h"

#include "core/LosslessJsonDocument.h"
#include "core/SaveDecoder.h"
#include "inventory/InventoryEditorPage.h"
#include "inventory/InventoryGridWidget.h"
#include "registry/ItemCatalog.h"
#include "settlement/SettlementManagerPage.h"
#include "ship/ShipManagerPage.h"
#include "ui/JsonExplorerPage.h"
#include "ui/MaterialLookupDialog.h"
#include "ui/WelcomePage.h"
#include "ui/LoadingOverlay.h"
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
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSplitter>
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

    sectionTree_ = new QTreeWidget(mainSplitter_);
    sectionTree_->setHeaderHidden(true);
    sectionTree_->setMinimumWidth(220);

    auto *homeItem = new QTreeWidgetItem(QStringList() << tr("Home"));
    homeItem->setData(0, Qt::UserRole, kPageHome);
    sectionTree_->addTopLevelItem(homeItem);

    auto *jsonItem = new QTreeWidgetItem(QStringList() << tr("JSON Explorer"));
    jsonItem->setData(0, Qt::UserRole, kPageJson);
    sectionTree_->addTopLevelItem(jsonItem);

    auto *inventoryItem = new QTreeWidgetItem(QStringList() << tr("Inventories"));
    inventoryItem->setData(0, Qt::UserRole, kPageInventory);
    sectionTree_->addTopLevelItem(inventoryItem);

    auto *currenciesItem = new QTreeWidgetItem(QStringList() << tr("Currencies"));
    currenciesItem->setData(0, Qt::UserRole, kPageCurrencies);
    sectionTree_->addTopLevelItem(currenciesItem);

    auto *expeditionItem = new QTreeWidgetItem(QStringList() << tr("Expedition"));
    expeditionItem->setData(0, Qt::UserRole, kPageExpedition);
    sectionTree_->addTopLevelItem(expeditionItem);

    auto *storageItem = new QTreeWidgetItem(QStringList() << tr("Storage Manager"));
    storageItem->setData(0, Qt::UserRole, kPageStorage);
    sectionTree_->addTopLevelItem(storageItem);

    auto *settlementItem = new QTreeWidgetItem(QStringList() << tr("Settlement Manager"));
    settlementItem->setData(0, Qt::UserRole, kPageSettlement);
    sectionTree_->addTopLevelItem(settlementItem);

    auto *shipItem = new QTreeWidgetItem(QStringList() << tr("Ship Manager"));
    shipItem->setData(0, Qt::UserRole, kPageShip);
    sectionTree_->addTopLevelItem(shipItem);

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

    stackedPages_->addWidget(welcomePage_);
    stackedPages_->addWidget(jsonPage_);
    stackedPages_->addWidget(inventoryPage_);
    stackedPages_->addWidget(settlementPage_);
    stackedPages_->addWidget(shipManagerPage_);
    stackedPages_->addWidget(currenciesPage_);
    stackedPages_->addWidget(expeditionPage_);
    stackedPages_->addWidget(storageManagerPage_);

    loadingOverlay_ = new LoadingOverlay(this);

    mainSplitter_->addWidget(sectionTree_);
    mainSplitter_->addWidget(stackedPages_);
    mainSplitter_->setStretchFactor(1, 1);

    const int gridWidth = InventoryGridWidget::preferredGridWidth();
    const int gridHeight = InventoryGridWidget::preferredGridHeight(6);
    const int treeWidth = sectionTree_->minimumWidth();
    resize(treeWidth + gridWidth + 60, gridHeight + 200);

    buildMenus();

    statusLabel_ = new QLabel(tr("Ready."), this);
    statusBar()->addWidget(statusLabel_);

    saveWatcher_ = new QFileSystemWatcher(this);
    connect(saveWatcher_, &QFileSystemWatcher::fileChanged, this, &MainWindow::handleSaveFileChanged);

    connect(sectionTree_, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *current, QTreeWidgetItem *) {
                if (!current) {
                    return;
                }
                QString key = current->data(0, Qt::UserRole).toString();
                if (stackedPages_->currentWidget() == jsonPage_ && key != kPageJson) {
                    if (!confirmLeaveJsonEditor(current->text(0))) {
                        QSignalBlocker blocker(sectionTree_);
                        for (int i = 0; i < sectionTree_->topLevelItemCount(); ++i) {
                            QTreeWidgetItem *item = sectionTree_->topLevelItem(i);
                            if (item && item->data(0, Qt::UserRole).toString() == kPageJson) {
                                sectionTree_->setCurrentItem(item);
                                break;
                            }
                        }
                        return;
                    }
                }
                if (key == kPageSettlement) {
                    openSettlementManager();
                    return;
                }
                if (key == kPageShip) {
                    openShipManager();
                    return;
                }
            if (key == kPageJson) {
                if (currentSaveFile_.isEmpty()) {
                    selectPage(kPageJson);
                    setStatus(tr("Load a save slot first."));
                    return;
                }
                openJsonEditor();
                return;
            }
            if (key == kPageInventory) {
                openInventoryEditor();
                return;
            }
                if (key == kPageCurrencies) {
                    openCurrenciesEditor();
                    return;
                }
                if (key == kPageExpedition) {
                    openExpeditionEditor();
                    return;
                }
                if (key == kPageStorage) {
                    openStorageManager();
                    return;
                }
                selectPage(key);
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

    connect(jsonPage_, &JsonExplorerPage::statusMessage, this, &MainWindow::setStatus);
    connect(inventoryPage_, &InventoryEditorPage::statusMessage, this, &MainWindow::setStatus);
    connect(currenciesPage_, &InventoryEditorPage::statusMessage, this, &MainWindow::setStatus);
    connect(expeditionPage_, &InventoryEditorPage::statusMessage, this, &MainWindow::setStatus);
    connect(storageManagerPage_, &InventoryEditorPage::statusMessage, this, &MainWindow::setStatus);
    connect(settlementPage_, &SettlementManagerPage::statusMessage, this, &MainWindow::setStatus);
    connect(shipManagerPage_, &ShipManagerPage::statusMessage, this, &MainWindow::setStatus);

    refreshSaveSlots();
    sectionTree_->setCurrentItem(homeItem);

    (void)QtConcurrent::run([]() {
        ItemCatalog::warmup();
    });

    shipManagerPage_->setSizePolicy(settlementPage_->sizePolicy());
}

void MainWindow::buildMenus()
{
    auto *fileMenu = menuBar()->addMenu(tr("File"));
    QAction *openAction = fileMenu->addAction(tr("Open Save..."));
    QAction *saveAction = fileMenu->addAction(tr("Save Changes"));
    QAction *saveAsAction = fileMenu->addAction(tr("Save As..."));
    QAction *exportAction = fileMenu->addAction(tr("Export JSON..."));
    fileMenu->addSeparator();
    QAction *exitAction = fileMenu->addAction(tr("Exit"));

    auto *viewMenu = menuBar()->addMenu(tr("View"));
    QAction *expandAction = viewMenu->addAction(tr("Expand All"));
    QAction *collapseAction = viewMenu->addAction(tr("Collapse All"));

    auto *helpMenu = menuBar()->addMenu(tr("Help"));
    QAction *logAction = helpMenu->addAction(tr("Open Log Folder"));
    QAction *aboutAction = helpMenu->addAction(tr("About"));

    connect(openAction, &QAction::triggered, this, &MainWindow::openJsonEditor);
    connect(saveAction, &QAction::triggered, this, &MainWindow::saveChanges);
    connect(saveAsAction, &QAction::triggered, this, [this]() {
        if (!jsonPage_->hasLoadedSave()) {
            setStatus(tr("No save loaded."));
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
        if (!jsonPage_->hasLoadedSave()) {
            setStatus(tr("No save loaded."));
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
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    connect(expandAction, &QAction::triggered, jsonPage_, &JsonExplorerPage::expandAll);
    connect(collapseAction, &QAction::triggered, jsonPage_, &JsonExplorerPage::collapseAll);

    connect(logAction, &QAction::triggered, this, []() {
        const QString logDir = QStringLiteral("/home/lotso/Documents/dev/NMS/NMSSaveExplorer-Qt");
        QDesktopServices::openUrl(QUrl::fromLocalFile(logDir));
    });

    connect(aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::information(this, tr("About Save Explorer"),
                                 tr("Save Explorer - No Man's Sky\nQt-based redesign scaffold."));
    });
}

void MainWindow::refreshSaveSlots()
{
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
    updateSaveWatcher(currentSaveFile_);
    welcomePage_->setSaveEnabled(false);
    welcomePage_->setLoadedSavePath(currentSaveFile_);
    setStatus(tr("Loaded %1").arg(QFileInfo(path).fileName()));
}

void MainWindow::openJsonEditor()
{
    if (currentSaveFile_.isEmpty()) {
        setStatus(tr("Load a save slot first."));
        selectPage(kPageJson);
        return;
    }

    if (loadingWatcher_.isRunning()) {
        return;
    }

    loadingOverlay_->showMessage(tr("Decoding save file, please wait..."));

    auto loadTask = [path = currentSaveFile_]() {
        QString error;
        QByteArray content = SaveDecoder::decodeSaveBytes(path, &error);
        
        LoadResult result;
        if (!content.isEmpty()) {
            result.lossless = std::make_shared<LosslessJsonDocument>();
            if (!result.lossless->parse(content, &error)) {
                result.error = error;
                return result;
            }
            QJsonParseError parseError;
            result.doc = QJsonDocument::fromJson(content, &parseError);
            if (parseError.error != QJsonParseError::NoError) {
                result.error = QObject::tr("JSON parse error: %1").arg(parseError.errorString());
            }
        }
        result.error = error;
        return result;
    };

    connect(&loadingWatcher_, &QFutureWatcher<LoadResult>::finished, this, [this]() {
        loadingOverlay_->hide();
        LoadResult result = loadingWatcher_.result();
        QJsonDocument doc = result.doc;
        QString error = result.error;

        if (!error.isEmpty() || doc.isNull()) {
            setStatus(error.isEmpty() ? tr("Failed to load JSON") : error);
            return;
        }

        jsonPage_->setRootDoc(doc, currentSaveFile_, result.lossless);
        welcomePage_->setSaveEnabled(jsonPage_->hasLoadedSave());
        welcomePage_->setLoadedSavePath(currentSaveFile_);
        updateSaveWatcher(currentSaveFile_);
        selectPage(kPageJson);
    }, Qt::SingleShotConnection);

    loadingWatcher_.setFuture(QtConcurrent::run(loadTask));
}

void MainWindow::openInventoryEditor()
{
    if (!confirmLeaveJsonEditor(tr("Inventories"))) {
        return;
    }
    if (currentSaveFile_.isEmpty()) {
        setStatus(tr("Load a save slot first."));
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

    QString error;
    if (!inventoryPage_->loadFromFile(path, &error)) {
        setStatus(error);
        return;
    }
    currentSaveFile_ = path;
    updateSaveWatcher(currentSaveFile_);
    welcomePage_->setSaveEnabled(inventoryPage_->hasLoadedSave());
    welcomePage_->setLoadedSavePath(currentSaveFile_);
    selectPage(kPageInventory);
}

void MainWindow::openCurrenciesEditor()
{
    if (!confirmLeaveJsonEditor(tr("Currencies"))) {
        return;
    }
    if (currentSaveFile_.isEmpty()) {
        setStatus(tr("Load a save slot first."));
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

    QString error;
    if (!currenciesPage_->loadFromFile(path, &error)) {
        setStatus(error);
        return;
    }
    currentSaveFile_ = path;
    updateSaveWatcher(currentSaveFile_);
    welcomePage_->setSaveEnabled(currenciesPage_->hasLoadedSave());
    welcomePage_->setLoadedSavePath(currentSaveFile_);
    selectPage(kPageCurrencies);
}

void MainWindow::openExpeditionEditor()
{
    if (!confirmLeaveJsonEditor(tr("Expedition"))) {
        return;
    }
    if (currentSaveFile_.isEmpty()) {
        setStatus(tr("Load a save slot first."));
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

    QString error;
    if (!expeditionPage_->loadFromFile(path, &error)) {
        setStatus(error);
        return;
    }
    currentSaveFile_ = path;
    updateSaveWatcher(currentSaveFile_);
    welcomePage_->setSaveEnabled(expeditionPage_->hasLoadedSave());
    welcomePage_->setLoadedSavePath(currentSaveFile_);
    selectPage(kPageExpedition);
}

void MainWindow::openStorageManager()
{
    if (!confirmLeaveJsonEditor(tr("Storage Manager"))) {
        return;
    }
    if (currentSaveFile_.isEmpty()) {
        setStatus(tr("Load a save slot first."));
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

    QString error;
    if (!storageManagerPage_->loadFromFile(path, &error)) {
        setStatus(error);
        return;
    }
    currentSaveFile_ = path;
    updateSaveWatcher(currentSaveFile_);
    welcomePage_->setSaveEnabled(storageManagerPage_->hasLoadedSave());
    welcomePage_->setLoadedSavePath(currentSaveFile_);
    selectPage(kPageStorage);
}

void MainWindow::openSettlementManager()
{
    if (!confirmLeaveJsonEditor(tr("Settlement Manager"))) {
        return;
    }
    if (currentSaveFile_.isEmpty()) {
        setStatus(tr("Load a save slot first."));
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

    QString error;
    if (!settlementPage_->loadFromFile(path, &error)) {
        setStatus(error);
        return;
    }
    currentSaveFile_ = path;
    updateSaveWatcher(currentSaveFile_);
    welcomePage_->setSaveEnabled(settlementPage_->hasLoadedSave());
    welcomePage_->setLoadedSavePath(currentSaveFile_);
    selectPage(kPageSettlement);
}

void MainWindow::openShipManager()
{
    if (!confirmLeaveJsonEditor(tr("Ship Manager"))) {
        return;
    }
    if (currentSaveFile_.isEmpty()) {
        setStatus(tr("Load a save slot first."));
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

    QString error;
    if (!shipManagerPage_->loadFromFile(path, &error)) {
        setStatus(error);
        return;
    }
    currentSaveFile_ = path;
    updateSaveWatcher(currentSaveFile_);
    welcomePage_->setSaveEnabled(shipManagerPage_->hasLoadedSave());
    welcomePage_->setLoadedSavePath(currentSaveFile_);
    selectPage(kPageShip);
}

void MainWindow::openMaterialLookup()
{
    MaterialLookupDialog dialog(this);
    dialog.exec();
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
    } else if (activePage == currenciesPage_ && currenciesPage_->hasLoadedSave()) {
        saved = currenciesPage_->saveChanges(&error);
    } else if (activePage == expeditionPage_ && expeditionPage_->hasLoadedSave()) {
        saved = expeditionPage_->saveChanges(&error);
    } else if (activePage == storageManagerPage_ && storageManagerPage_->hasLoadedSave()) {
        saved = storageManagerPage_->saveChanges(&error);
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

void MainWindow::selectPage(const QString &key)
{
    if (key == kPageHome) {
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
        stackedPages_->setCurrentIndex(5);
    } else if (key == kPageExpedition) {
        stackedPages_->setCurrentIndex(6);
    } else if (key == kPageStorage) {
        stackedPages_->setCurrentIndex(7);
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
    QMessageBox::StandardButton response = QMessageBox::warning(
        this,
        tr("Unsaved JSON Changes"),
        tr("You have unsaved changes in the JSON Explorer.\n"
           "Do you want to discard them and open %1?").arg(nextAction),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    return response == QMessageBox::Yes;
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
