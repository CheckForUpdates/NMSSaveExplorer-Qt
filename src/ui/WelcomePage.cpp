#include "ui/WelcomePage.h"

#include "core/JsonMapper.h"
#include "core/ResourceLocator.h"
#include "core/SaveDecoder.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLocale>
#include <QPalette>
#include <QPushButton>
#include <QSizePolicy>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
const char *kMappingFile = "mapping.json";

struct SaveSlotSummary {
    QString name;
    QString gameMode;
    QString totalPlayTime;
    QString location;
};

void ensureMappingLoaded()
{
    if (JsonMapper::isLoaded()) {
        return;
    }
    QString mappingPath = ResourceLocator::resolveResource(kMappingFile);
    JsonMapper::loadMapping(mappingPath);
}

QJsonValue findMappedKey(const QJsonValue &value, const QString &key)
{
    if (value.isObject()) {
        QJsonObject obj = value.toObject();
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            if (JsonMapper::mapKey(it.key()) == key) {
                return it.value();
            }
        }
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            QJsonValue nested = findMappedKey(it.value(), key);
            if (!nested.isUndefined()) {
                return nested;
            }
        }
    } else if (value.isArray()) {
        QJsonArray arr = value.toArray();
        for (const QJsonValue &element : arr) {
            QJsonValue nested = findMappedKey(element, key);
            if (!nested.isUndefined()) {
                return nested;
            }
        }
    }
    return QJsonValue(QJsonValue::Undefined);
}

QJsonValue findMappedTopLevelKey(const QJsonValue &value, const QString &key)
{
    if (!value.isObject()) {
        return QJsonValue(QJsonValue::Undefined);
    }
    QJsonObject obj = value.toObject();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        if (JsonMapper::mapKey(it.key()) == key) {
            return it.value();
        }
    }
    return QJsonValue(QJsonValue::Undefined);
}

QJsonValue findRawKey(const QJsonValue &value, const QString &key)
{
    if (value.isObject()) {
        QJsonObject obj = value.toObject();
        auto it = obj.find(key);
        if (it != obj.end()) {
            return it.value();
        }
        for (auto nestedIt = obj.begin(); nestedIt != obj.end(); ++nestedIt) {
            QJsonValue nested = findRawKey(nestedIt.value(), key);
            if (!nested.isUndefined()) {
                return nested;
            }
        }
    } else if (value.isArray()) {
        QJsonArray arr = value.toArray();
        for (const QJsonValue &element : arr) {
            QJsonValue nested = findRawKey(element, key);
            if (!nested.isUndefined()) {
                return nested;
            }
        }
    }
    return QJsonValue(QJsonValue::Undefined);
}

QString formatPlayTime(const QJsonValue &value)
{
    if (value.isString()) {
        return value.toString();
    }
    if (!value.isDouble()) {
        return QString();
    }
    double seconds = value.toDouble();
    if (seconds < 0) {
        return QString();
    }
    qint64 totalSeconds = static_cast<qint64>(seconds);
    qint64 hours = totalSeconds / 3600;
    qint64 minutes = (totalSeconds / 60) % 60;
    return QString("%1:%2").arg(hours).arg(minutes, 2, 10, QChar('0'));
}

QString formatGameMode(const QJsonValue &value)
{
    if (value.isString()) {
        QString text = value.toString();
        if (text.startsWith("GameMode_", Qt::CaseInsensitive)) {
            text = text.mid(QString("GameMode_").size());
        }
        text.replace('_', ' ');
        return text;
    }
    if (!value.isDouble()) {
        return QString();
    }
    int mode = static_cast<int>(value.toDouble());
    switch (mode) {
    case 0:
        return QStringLiteral("Normal");
    case 1:
        return QStringLiteral("Survival");
    case 2:
        return QStringLiteral("Permadeath");
    case 3:
        return QStringLiteral("Creative");
    case 4:
        return QStringLiteral("Expedition");
    case 5:
        return QStringLiteral("Custom");
    default:
        return QString();
    }
}

int locationMatchScore(const QString &candidate, const QString &needle)
{
    if (candidate.isEmpty()) {
        return -1;
    }
    if (!needle.isEmpty() && !candidate.contains(needle)) {
        return -1;
    }
    int score = 1000 - qMin(candidate.size(), 900);
    if (!candidate.isEmpty() && candidate.at(0).isUpper()) {
        score += 50;
    }
    if (candidate.startsWith("Settlement", Qt::CaseInsensitive)) {
        score += 100;
    }
    if (candidate.startsWith("On ", Qt::CaseInsensitive)) {
        score += 80;
    }
    if (candidate.endsWith(needle)) {
        score += 30;
    }
    return score;
}

void findBestLocationMatch(const QJsonValue &value, const QString &needle,
                           QString *best, int *bestScore)
{
    if (value.isString()) {
        QString candidate = value.toString().trimmed();
        int score = locationMatchScore(candidate, needle);
        if (score > *bestScore) {
            *bestScore = score;
            *best = candidate;
        }
        return;
    }
    if (value.isObject()) {
        QJsonObject obj = value.toObject();
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            findBestLocationMatch(it.value(), needle, best, bestScore);
        }
    } else if (value.isArray()) {
        QJsonArray arr = value.toArray();
        for (const QJsonValue &element : arr) {
            findBestLocationMatch(element, needle, best, bestScore);
        }
    }
}

SaveSlotSummary loadSummary(const SaveSlot &slot)
{
    SaveSlotSummary summary;
    if (slot.latestSave.isEmpty()) {
        return summary;
    }

    QString content;
    if (slot.latestSave.endsWith(".hg", Qt::CaseInsensitive)) {
        content = SaveDecoder::decodeSave(slot.latestSave, nullptr);
    } else {
        QFile file(slot.latestSave);
        if (file.open(QIODevice::ReadOnly)) {
            content = QString::fromUtf8(file.readAll());
        }
    }
    if (content.isEmpty()) {
        return summary;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(content.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return summary;
    }

    ensureMappingLoaded();
    QJsonValue rootValue = doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array());

    QJsonValue contextValue = rootValue;
    QString activeContext = findMappedKey(rootValue, QStringLiteral("ActiveContext")).toString().trimmed();
    QString contextKey = QStringLiteral("BaseContext");
    if (activeContext.compare(QStringLiteral("Expedition"), Qt::CaseInsensitive) == 0) {
        contextKey = QStringLiteral("ExpeditionContext");
    }
    QJsonValue foundContext = findMappedTopLevelKey(rootValue, contextKey);
    if (!foundContext.isUndefined()) {
        contextValue = foundContext;
    }

    QJsonValue saveNameValue = findMappedKey(rootValue, QStringLiteral("SaveName"));
    if (saveNameValue.isString()) {
        summary.name = saveNameValue.toString().trimmed();
    }

    QJsonValue playTimeValue = findMappedKey(rootValue, QStringLiteral("TotalPlayTime"));
    if (playTimeValue.isUndefined()) {
        playTimeValue = findRawKey(rootValue, QStringLiteral("Lg8"));
    }
    summary.totalPlayTime = formatPlayTime(playTimeValue);

    QJsonValue gameModeValue = findMappedKey(contextValue, QStringLiteral("GameMode"));
    QJsonValue presetModeValue;
    if (gameModeValue.isObject() || gameModeValue.isArray()) {
        presetModeValue = findMappedKey(gameModeValue, QStringLiteral("PresetGameMode"));
    }
    if (presetModeValue.isUndefined()) {
        presetModeValue = findMappedKey(contextValue, QStringLiteral("PresetGameMode"));
    }
    if (presetModeValue.isUndefined()) {
        presetModeValue = gameModeValue;
    }
    QString gameModeLabel = formatGameMode(presetModeValue);

    QJsonValue difficultyPresetValue = findMappedKey(contextValue, QStringLiteral("DifficultyPresetType"));
    QString difficultyPresetLabel = formatGameMode(difficultyPresetValue);
    if (!difficultyPresetLabel.isEmpty()) {
        if (difficultyPresetLabel.compare(QStringLiteral("Custom"), Qt::CaseInsensitive) == 0
            || gameModeLabel.isEmpty()) {
            gameModeLabel = difficultyPresetLabel;
        }
    }
    summary.gameMode = gameModeLabel;

    QString location = slot.locationName.trimmed();
    if (!location.isEmpty() && !location.at(0).isUpper()) {
        QString best;
        int bestScore = -1;
        findBestLocationMatch(rootValue, location, &best, &bestScore);
        if (!best.isEmpty()) {
            location = best;
        }
    }
    summary.location = location;
    return summary;
}
}

WelcomePage::WelcomePage(QWidget *parent)
    : QWidget(parent)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);

    auto *container = new QWidget(this);
    container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    mainLayout->addWidget(container);
    mainLayout->addStretch();

    auto *title = new QLabel(tr("No Man's Sky Save Manager"), container);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 6);
    titleFont.setBold(true);
    title->setFont(titleFont);

    headingLabel_ = new QLabel(tr("Select a save slot to begin."), container);

    layout->addWidget(title);
    layout->addWidget(headingLabel_);

    layout->addSpacing(6);
    layout->addWidget(new QLabel(tr("Detected save slots:"), container));

    slotTable_ = new QTableWidget(container);
    slotTable_->setColumnCount(6);
    slotTable_->setHorizontalHeaderLabels(QStringList()
                                          << tr("Slot")
                                          << tr("Game Mode")
                                          << tr("Name")
                                          << tr("Location")
                                          << tr("Total Play Time")
                                          << tr("Last Save"));
    slotTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    slotTable_->setSelectionMode(QAbstractItemView::NoSelection);
    slotTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    slotTable_->setShowGrid(true);
    slotTable_->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    slotTable_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    slotTable_->verticalHeader()->setVisible(false);
    slotTable_->setFocusPolicy(Qt::NoFocus);
    slotTable_->setStyleSheet(QStringLiteral("QTableWidget::item:selected { background: transparent; color: inherit; }"));
    
    QHeaderView *header = slotTable_->horizontalHeader();
    header->setSectionResizeMode(QHeaderView::Interactive);
    
    layout->addWidget(slotTable_);

    layout->addSpacing(6);
    layout->addWidget(new QLabel(tr("Saves in selected slot:"), container));

    saveTable_ = new QTableWidget(container);
    saveTable_->setColumnCount(3);
    saveTable_->setHorizontalHeaderLabels(QStringList()
                                          << tr("Save")
                                          << tr("Last Save")
                                          << tr("Synced"));
    saveTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    saveTable_->setSelectionMode(QAbstractItemView::NoSelection);
    saveTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    saveTable_->setShowGrid(true);
    saveTable_->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    saveTable_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    saveTable_->verticalHeader()->setVisible(false);
    saveTable_->setFocusPolicy(Qt::NoFocus);
    saveTable_->setStyleSheet(QStringLiteral("QTableWidget::item:selected { background: transparent; color: inherit; }"));

    QHeaderView *saveHeader = saveTable_->horizontalHeader();
    saveHeader->setSectionResizeMode(QHeaderView::Interactive);

    layout->addWidget(saveTable_);

    auto *refreshRow = new QHBoxLayout();
    loadButton_ = new QPushButton(tr("Load"), container);
    loadButton_->setEnabled(false);
    auto *refreshButton = new QPushButton(tr("Refresh"), container);
    auto *browseButton = new QPushButton(tr("Browse..."), container);
    auto *materialLookupButton = new QPushButton(tr("Material Lookup"), container);
    syncButton_ = new QPushButton(tr("Sync Saves"), container);
    syncButton_->setEnabled(false);
    undoSyncButton_ = new QPushButton(tr("Undo Sync"), container);
    undoSyncButton_->setEnabled(false);
    saveChangesButton_ = new QPushButton(tr("Save Changes"), container);
    saveChangesButton_->setProperty("canSave", false);
    
    refreshRow->addWidget(loadButton_);
    refreshRow->addWidget(refreshButton);
    refreshRow->addWidget(browseButton);
    refreshRow->addWidget(materialLookupButton);
    refreshRow->addWidget(syncButton_);
    refreshRow->addWidget(undoSyncButton_);
    refreshRow->addWidget(saveChangesButton_);
    refreshRow->addStretch();
    layout->addLayout(refreshRow);

    connect(refreshButton, &QPushButton::clicked, this, &WelcomePage::refreshRequested);
    connect(browseButton, &QPushButton::clicked, this, &WelcomePage::browseRequested);
    connect(materialLookupButton, &QPushButton::clicked, this, &WelcomePage::materialLookupRequested);
    connect(saveChangesButton_, &QPushButton::clicked, this, &WelcomePage::saveChangesRequested);
    connect(syncButton_, &QPushButton::clicked, this, &WelcomePage::syncOtherSaveRequested);
    connect(undoSyncButton_, &QPushButton::clicked, this, &WelcomePage::undoSyncRequested);
    connect(loadButton_, &QPushButton::clicked, this, [this]() {
        emit loadSaveRequested();
    });
    connect(slotTable_, &QTableWidget::cellClicked, this, [this](int row, int) {
        updateSlotSelection(row);
        SaveSlot slot = selectedSlot();
        updateSaveFilesTable(slot);
        updateButtonState();
    });
    connect(saveTable_, &QTableWidget::cellClicked, this, [this](int row, int) {
        updateSaveSelection(row);
        updateButtonState();
    });

    updateButtonState();
}

void WelcomePage::setSlots(const QList<SaveSlot> &saveSlots)
{
    saveSlots_ = saveSlots;
    slotTable_->clearContents();
    slotTable_->setRowCount(saveSlots.size());
    for (int row = 0; row < saveSlots.size(); ++row) {
        const SaveSlot &slot = saveSlots.at(row);
        SaveSlotSummary summary = loadSummary(slot);
        QString slotLabel = QString::number(row + 1);
        auto *slotItem = new QTableWidgetItem(slotLabel);
        slotItem->setData(Qt::UserRole, QVariant::fromValue(slot));
        slotItem->setTextAlignment(Qt::AlignCenter);
        slotItem->setData(Qt::UserRole + 1, slotItem->font());
        slotItem->setData(Qt::UserRole + 2, slotTable_->palette().brush(QPalette::Text));
        slotTable_->setItem(row, 0, slotItem);

        QString modeLabel = summary.gameMode.isEmpty() ? tr("Unknown") : summary.gameMode;
        auto *modeItem = new QTableWidgetItem(modeLabel);
        modeItem->setTextAlignment(Qt::AlignCenter);
        modeItem->setData(Qt::UserRole + 1, modeItem->font());
        modeItem->setData(Qt::UserRole + 2, slotTable_->palette().brush(QPalette::Text));
        slotTable_->setItem(row, 1, modeItem);

        auto *nameItem = new QTableWidgetItem(summary.name);
        nameItem->setData(Qt::UserRole + 1, nameItem->font());
        nameItem->setData(Qt::UserRole + 2, slotTable_->palette().brush(QPalette::Text));
        slotTable_->setItem(row, 2, nameItem);

        QString locationLabel = summary.location.isEmpty() ? tr("Unknown") : summary.location;
        auto *locationItem = new QTableWidgetItem(locationLabel);
        locationItem->setData(Qt::UserRole + 1, locationItem->font());
        locationItem->setData(Qt::UserRole + 2, slotTable_->palette().brush(QPalette::Text));
        slotTable_->setItem(row, 3, locationItem);

        QString playTimeLabel = summary.totalPlayTime.isEmpty() ? tr("Unknown") : summary.totalPlayTime;
        auto *playTimeItem = new QTableWidgetItem(playTimeLabel);
        playTimeItem->setTextAlignment(Qt::AlignCenter);
        playTimeItem->setData(Qt::UserRole + 1, playTimeItem->font());
        playTimeItem->setData(Qt::UserRole + 2, slotTable_->palette().brush(QPalette::Text));
        slotTable_->setItem(row, 4, playTimeItem);

        QString lastSave = tr("Unknown");
        if (slot.lastModified > 0) {
            QDateTime lastSaveTime = QDateTime::fromMSecsSinceEpoch(slot.lastModified);
            lastSave = QLocale::system().toString(lastSaveTime, QLocale::ShortFormat);
        }
        auto *lastSaveItem = new QTableWidgetItem(lastSave);
        lastSaveItem->setTextAlignment(Qt::AlignCenter);
        lastSaveItem->setData(Qt::UserRole + 1, lastSaveItem->font());
        lastSaveItem->setData(Qt::UserRole + 2, slotTable_->palette().brush(QPalette::Text));
        slotTable_->setItem(row, 5, lastSaveItem);
    }
    slotTable_->resizeColumnsToContents();
    if (!saveSlots.isEmpty()) {
        headingLabel_->setText(tr("Detected %1 save slot(s).").arg(saveSlots.size()));
    } else {
        headingLabel_->setText(tr("No save slots found automatically."));
    }
    if (!saveSlots.isEmpty()) {
        updateSlotSelection(0);
        updateSaveFilesTable(saveSlots.first());
    } else {
        slotTable_->setCurrentItem(nullptr);
        slotTable_->clearSelection();
        saveTable_->clearContents();
        saveTable_->setRowCount(0);
        selectedSlotRow_ = -1;
    }
    updateButtonState();
}

SaveSlot WelcomePage::selectedSlot() const
{
    if (selectedSlotRow_ >= 0) {
        QTableWidgetItem *item = slotTable_->item(selectedSlotRow_, 0);
        if (item) {
            QVariant data = item->data(Qt::UserRole);
            if (data.canConvert<SaveSlot>()) {
                return data.value<SaveSlot>();
            }
        }
    }
    return {};
}

void WelcomePage::updateSaveFilesTable(const SaveSlot &slot)
{
    saveTable_->clearContents();
    saveTable_->setRowCount(slot.saveFiles.size());
    selectedSaveRow_ = -1;
    selectedSavePath_.clear();

    int loadedRow = -1;
    QByteArray primaryBytes;
    bool canCompare = slot.saveFiles.size() == 2;
    QString primaryPath;
    QByteArray secondaryBytes;
    bool savesMatch = false;
    if (canCompare) {
        primaryPath = slot.saveFiles.at(0).filePath;
        QFile primaryFile(primaryPath);
        if (primaryFile.open(QIODevice::ReadOnly)) {
            primaryBytes = primaryFile.readAll();
        } else {
            canCompare = false;
        }
        if (canCompare) {
            QFile secondaryFile(slot.saveFiles.at(1).filePath);
            if (secondaryFile.open(QIODevice::ReadOnly)) {
                secondaryBytes = secondaryFile.readAll();
                savesMatch = (secondaryBytes == primaryBytes);
            } else {
                canCompare = false;
            }
        }
    }

    for (int row = 0; row < slot.saveFiles.size(); ++row) {
        const SaveSlot::SaveFileEntry &entry = slot.saveFiles.at(row);
        QString fileName = QFileInfo(entry.filePath).fileName();
        QString displayName = fileName;
        bool isLoaded = (!loadedSavePath_.isEmpty() && entry.filePath == loadedSavePath_);
        if (isLoaded) {
            displayName = QString("%1 (%2)").arg(displayName, tr("Loaded"));
            loadedRow = row;
        }
        auto *nameItem = new QTableWidgetItem(displayName);
        nameItem->setData(Qt::UserRole, entry.filePath);
        nameItem->setData(Qt::UserRole + 1, nameItem->font());
        nameItem->setData(Qt::UserRole + 2, saveTable_->palette().brush(QPalette::Text));
        saveTable_->setItem(row, 0, nameItem);

        QString lastSave = tr("Unknown");
        if (entry.lastModified > 0) {
            QDateTime lastSaveTime = QDateTime::fromMSecsSinceEpoch(entry.lastModified);
            lastSave = QLocale::system().toString(lastSaveTime, QLocale::ShortFormat);
        }
        auto *timeItem = new QTableWidgetItem(lastSave);
        timeItem->setTextAlignment(Qt::AlignCenter);
        timeItem->setData(Qt::UserRole + 1, timeItem->font());
        timeItem->setData(Qt::UserRole + 2, saveTable_->palette().brush(QPalette::Text));
        saveTable_->setItem(row, 1, timeItem);

        QString syncedLabel = (canCompare && savesMatch) ? tr("Yes") : tr("No");
        auto *syncedItem = new QTableWidgetItem(syncedLabel);
        syncedItem->setTextAlignment(Qt::AlignCenter);
        syncedItem->setData(Qt::UserRole + 1, syncedItem->font());
        syncedItem->setData(Qt::UserRole + 2, saveTable_->palette().brush(QPalette::Text));
        saveTable_->setItem(row, 2, syncedItem);
    }
    saveTable_->resizeColumnsToContents();

    if (loadedRow >= 0) {
        setRowBold(saveTable_, loadedRow, true);
    }
    saveTable_->setCurrentItem(nullptr);
    saveTable_->clearSelection();
}

QString WelcomePage::selectedSavePath() const
{
    if (selectedSaveRow_ >= 0) {
        QTableWidgetItem *item = saveTable_->item(selectedSaveRow_, 0);
        if (item) {
            return item->data(Qt::UserRole).toString();
        }
    }
    return QString();
}

QString WelcomePage::otherSavePathForSelection() const
{
    QString selected = selectedSavePath();
    if (selected.isEmpty()) {
        return QString();
    }
    SaveSlot slot = selectedSlot();
    for (const SaveSlot::SaveFileEntry &entry : slot.saveFiles) {
        if (entry.filePath != selected) {
            return entry.filePath;
        }
    }
    return QString();
}

bool WelcomePage::hasOtherSaveForSelection() const
{
    SaveSlot slot = selectedSlot();
    return slot.saveFiles.size() > 1;
}

void WelcomePage::updateButtonState()
{
    bool hasSelection = selectedSaveRow_ >= 0 || !loadedSavePath_.isEmpty();
    bool canSave = saveChangesButton_->property("canSave").toBool();
    bool canSync = hasOtherSaveForSelection() && !syncPending_;
    bool canUndo = syncPending_ || syncApplied_;

    saveChangesButton_->setEnabled(syncPending_ || (canSave && hasSelection));
    if (syncButton_) {
        syncButton_->setEnabled(canSync);
    }
    if (undoSyncButton_) {
        undoSyncButton_->setEnabled(canUndo);
    }
    if (loadButton_) {
        loadButton_->setEnabled(!selectedSavePath_.isEmpty() && selectedSavePath_ != loadedSavePath_);
    }
}

void WelcomePage::setSaveEnabled(bool enabled)
{
    saveChangesButton_->setProperty("canSave", enabled);
    updateButtonState();
}

void WelcomePage::setLoadedSavePath(const QString &path)
{
    loadedSavePath_ = path;
    int loadedSlotRow = -1;
    for (int row = 0; row < slotTable_->rowCount(); ++row) {
        QTableWidgetItem *slotItem = slotTable_->item(row, 0);
        if (!slotItem) {
            continue;
        }
        QVariant data = slotItem->data(Qt::UserRole);
        if (!data.canConvert<SaveSlot>()) {
            continue;
        }
        SaveSlot slot = data.value<SaveSlot>();
        QString slotLabel = QString::number(row + 1);
        if (!loadedSavePath_.isEmpty()) {
            for (const SaveSlot::SaveFileEntry &entry : slot.saveFiles) {
                if (entry.filePath == loadedSavePath_) {
                    loadedSlotRow = row;
                    break;
                }
            }
        }
        slotItem->setText(slotLabel);
        for (int col = 0; col < slotTable_->columnCount(); ++col) {
            QTableWidgetItem *item = slotTable_->item(row, col);
            if (!item) {
                continue;
            }
            QVariant baseFont = item->data(Qt::UserRole + 1);
            QFont font = baseFont.canConvert<QFont>() ? baseFont.value<QFont>() : item->font();
            font.setBold(false);
            item->setFont(font);
        }
    }
    selectedSlotRow_ = -1;
    if (selectedSaveRow_ >= 0) {
        setRowBold(saveTable_, selectedSaveRow_, false);
    }
    selectedSaveRow_ = -1;
    selectedSavePath_.clear();
    if (loadedSlotRow >= 0) {
        updateSlotSelection(loadedSlotRow);
        SaveSlot slot = selectedSlot();
        updateSaveFilesTable(slot);
    } else {
        updateSaveFilesTable(selectedSlot());
    }
    updateButtonState();
}

void WelcomePage::setSyncState(bool pending, bool applied)
{
    syncPending_ = pending;
    syncApplied_ = applied;
    updateButtonState();
}

void WelcomePage::setRowBold(QTableWidget *table, int row, bool bold)
{
    if (!table || row < 0) {
        return;
    }
    for (int col = 0; col < table->columnCount(); ++col) {
        QTableWidgetItem *item = table->item(row, col);
        if (!item) {
            continue;
        }
        QVariant baseFont = item->data(Qt::UserRole + 1);
        QFont font = baseFont.canConvert<QFont>() ? baseFont.value<QFont>() : item->font();
        font.setBold(bold);
        item->setFont(font);
        QVariant baseBrush = item->data(Qt::UserRole + 2);
        if (bold) {
            QBrush brush = table->palette().brush(QPalette::Text);
            item->setForeground(brush);
            item->setData(Qt::ForegroundRole, brush);
        } else if (baseBrush.canConvert<QBrush>()) {
            QBrush brush = baseBrush.value<QBrush>();
            item->setForeground(brush);
            item->setData(Qt::ForegroundRole, brush);
        } else {
            QBrush brush = table->palette().brush(QPalette::Text);
            item->setForeground(brush);
            item->setData(Qt::ForegroundRole, brush);
        }
    }
    table->resizeColumnsToContents();
}

void WelcomePage::updateSlotSelection(int row)
{
    if (selectedSlotRow_ >= 0) {
        setRowBold(slotTable_, selectedSlotRow_, false);
    }
    selectedSlotRow_ = row;
    if (row >= 0) {
        setRowBold(slotTable_, row, true);
    }
}

void WelcomePage::updateSaveSelection(int row)
{
    if (selectedSaveRow_ >= 0) {
        bool wasLoaded = false;
        QTableWidgetItem *item = saveTable_->item(selectedSaveRow_, 0);
        if (item && item->data(Qt::UserRole).toString() == loadedSavePath_) {
            wasLoaded = true;
        }
        if (!wasLoaded) {
            setRowBold(saveTable_, selectedSaveRow_, false);
        }
    }
    selectedSaveRow_ = row;
    selectedSavePath_.clear();
    if (row >= 0) {
        QTableWidgetItem *item = saveTable_->item(row, 0);
        if (item) {
            selectedSavePath_ = item->data(Qt::UserRole).toString();
        }
        setRowBold(saveTable_, row, true);
    }
}
