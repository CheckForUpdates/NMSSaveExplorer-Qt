#include "corvette/CorvetteManagerPage.h"

#include "core/JsonMapper.h"
#include "core/LosslessJsonDocument.h"
#include "core/ResourceLocator.h"
#include "core/SaveDecoder.h"
#include "core/SaveEncoder.h"
#include "core/SaveJsonModel.h"
#include "inventory/InventoryGridWidget.h"

#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QComboBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStandardPaths>

namespace {
const char *kKeyActiveContext = "XTp";
const char *kKeyExpeditionContext = "2YS";
const char *kKeyPlayerState = "vLc";
const char *kKeyContextMain = "Main";
const char *kKeyActiveContextLong = "ActiveContext";
const char *kKeyExpeditionContextLong = "ExpeditionContext";
const char *kKeyBaseContextLong = "BaseContext";
const char *kKeyPlayerStateLong = "PlayerStateData";

// Corvette markers from mapping.json
const char *kKeyCorvetteInventory = "wem"; // CorvetteStorageInventory
const char *kKeyCorvetteLayout = "9i?";    // CorvetteStorageLayout
const char *kKeyCorvetteName = "tVi";      // CorvetteEditShipName
const char *kKeyCorvetteSeed = "60t";      // CorvetteDraftShipSeed
const char *kKeyTechInventory = "PMT";     // Inventory_TechOnly (general)
const char *kKeyCorvetteInventoryLong = "CorvetteStorageInventory";
const char *kKeyCorvetteLayoutLong = "CorvetteStorageLayout";
const char *kKeyCorvetteNameLong = "CorvetteEditShipName";
const char *kKeyCorvetteSeedLong = "CorvetteDraftShipSeed";
const char *kKeyTechInventoryLong = "Inventory_TechOnly";
const char *kKeySlots = ":No";
const char *kKeySlotsLong = "Slots";
const char *kKeyValidSlots = "hl?";
const char *kKeyValidSlotsLong = "ValidSlotIndices";
const char *kKeySpecialSlots = "MMm";
const char *kKeySpecialSlotsLong = "SpecialSlots";

void localEnsureMappingLoaded()
{
    if (JsonMapper::isLoaded()) {
        return;
    }
    QString mappingPath = ResourceLocator::resolveResource("mapping.json");
    JsonMapper::loadMapping(mappingPath);
}

QString findTopLevelMappedKeyName(const QJsonObject &root, const QString &key)
{
    auto it = root.find(key);
    if (it != root.end()) {
        return it.key();
    }
    localEnsureMappingLoaded();
    for (auto it = root.begin(); it != root.end(); ++it) {
        if (JsonMapper::mapKey(it.key()) == key) {
            return it.key();
        }
    }
    return QString();
}

QJsonValue findMappedKey(const QJsonValue &value, const QString &key)
{
    if (value.isObject()) {
        QJsonObject obj = value.toObject();
        auto it = obj.find(key);
        if (it != obj.end()) {
            return it.value();
        }
        localEnsureMappingLoaded();
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            if (JsonMapper::mapKey(it.key()) == key) {
                return it.value();
            }
            QJsonValue nested = findMappedKey(it.value(), key);
            if (!nested.isUndefined()) {
                return nested;
            }
        }
    } else if (value.isArray()) {
        for (const QJsonValue &element : value.toArray()) {
            QJsonValue nested = findMappedKey(element, key);
            if (!nested.isUndefined()) {
                return nested;
            }
        }
    }
    return {};
}

QString resolveKeyName(const QJsonObject &obj, const char *shortKey, const char *longKey)
{
    if (obj.contains(longKey)) {
        return QString::fromUtf8(longKey);
    }
    if (obj.contains(shortKey)) {
        return QString::fromUtf8(shortKey);
    }
    localEnsureMappingLoaded();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        if (JsonMapper::mapKey(it.key()) == QString::fromUtf8(longKey)) {
            return it.key();
        }
    }
    return QString();
}

QJsonValue valueForKey(const QJsonObject &obj, const char *shortKey, const char *longKey)
{
    QString key = resolveKeyName(obj, shortKey, longKey);
    if (key.isEmpty()) {
        return {};
    }
    return obj.value(key);
}

bool jsonValuesEqual(const QJsonValue &left, const QJsonValue &right)
{
    if (left.type() != right.type()) {
        return false;
    }
    if (left.isObject()) {
        return left.toObject() == right.toObject();
    }
    if (left.isArray()) {
        return left.toArray() == right.toArray();
    }
    return left == right;
}
}

CorvetteManagerPage::CorvetteManagerPage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

void CorvetteManagerPage::buildUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(16);

    // Top selection and action area
    auto *topLayout = new QHBoxLayout();
    topLayout->setSpacing(12);

    auto *label = new QLabel(tr("Select Corvette:"), this);
    corvetteCombo_ = new QComboBox(this);
    corvetteCombo_->setMinimumWidth(300);
    connect(corvetteCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CorvetteManagerPage::onCorvetteSelected);

    importButton_ = new QPushButton(tr("Import"), this);
    exportButton_ = new QPushButton(tr("Export"), this);
    useButton_ = new QPushButton(tr("Use"), this);

    connect(importButton_, &QPushButton::clicked, this, &CorvetteManagerPage::onImportClicked);
    connect(exportButton_, &QPushButton::clicked, this, &CorvetteManagerPage::onExportClicked);
    connect(useButton_, &QPushButton::clicked, this, &CorvetteManagerPage::onUseClicked);

    topLayout->addWidget(label);
    topLayout->addWidget(corvetteCombo_);
    topLayout->addWidget(importButton_);
    topLayout->addWidget(exportButton_);
    topLayout->addWidget(useButton_);
    topLayout->addStretch();

    mainLayout->addLayout(topLayout);

    // Tabs for Inventory and Tech
    tabs_ = new QTabWidget(this);
    inventoryGrid_ = new InventoryGridWidget(this);
    techGrid_ = new InventoryGridWidget(this);

    auto *invScroll = new QScrollArea(this);
    invScroll->setWidgetResizable(true);
    invScroll->setWidget(inventoryGrid_);

    auto *techScroll = new QScrollArea(this);
    techScroll->setWidgetResizable(true);
    techScroll->setWidget(techGrid_);

    tabs_->addTab(invScroll, tr("Inventory"));
    tabs_->addTab(techScroll, tr("Technology"));

    mainLayout->addWidget(tabs_);
}

bool CorvetteManagerPage::loadFromFile(const QString &filePath, QString *errorMessage)
{
    currentFilePath_ = filePath;
    QString content = SaveDecoder::decodeSave(filePath, errorMessage);
    if (content.isEmpty()) {
        return false;
    }

    QJsonParseError parseError;
    rootDoc_ = QJsonDocument::fromJson(content.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (errorMessage) {
            *errorMessage = tr("JSON parse error: %1").arg(parseError.errorString());
        }
        return false;
    }

    losslessDoc_ = std::make_shared<LosslessJsonDocument>();
    if (!losslessDoc_->parse(content.toUtf8())) {
        if (errorMessage) {
            *errorMessage = tr("Failed to load lossless JSON.");
        }
        return false;
    }

    updateActiveContext();
    if (!playerHasCorvetteData(usingExpeditionContext_)) {
        bool alternate = !usingExpeditionContext_;
        if (playerHasCorvetteData(alternate)) {
            usingExpeditionContext_ = alternate;
        }
    }
    loadLocalCorvettes();
    rebuildCorvetteList();
    refreshGrids();

    hasUnsavedChanges_ = false;
    return true;
}

bool CorvetteManagerPage::saveChanges(QString *errorMessage)
{
    if (!hasLoadedSave()) {
        if (errorMessage) *errorMessage = tr("No save loaded.");
        return false;
    }

    QByteArray docBytes;
    if (losslessDoc_) {
        docBytes = losslessDoc_->toJson(false);
    } else {
        docBytes = rootDoc_.toJson(QJsonDocument::Compact);
    }

    if (!SaveEncoder::encodeSave(currentFilePath_, docBytes, errorMessage)) {
        return false;
    }

    hasUnsavedChanges_ = false;
    return true;
}

void CorvetteManagerPage::loadLocalCorvettes()
{
    localCorvettes_.clear();
    QDir dir(localCorvettesPath());
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    QStringList filters;
    filters << "*.json";
    QFileInfoList list = dir.entryInfoList(filters, QDir::Files);
    for (const QFileInfo &info : list) {
        CorvetteEntry entry;
        entry.fileName = info.fileName();
        entry.name = info.baseName();
        QFile file(info.absoluteFilePath());
        if (file.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            if (doc.isObject()) {
                QJsonObject obj = doc.object();
                QString name = valueForKey(obj, kKeyCorvetteName, kKeyCorvetteNameLong).toString();
                if (!name.isEmpty()) {
                    entry.name = name;
                }
                entry.seed = valueForKey(obj, kKeyCorvetteSeed, kKeyCorvetteSeedLong);
            }
        }
        localCorvettes_.append(entry);
    }
}

void CorvetteManagerPage::rebuildCorvetteList()
{
    corvetteCombo_->blockSignals(true);
    corvetteCombo_->clear();

    QJsonObject player = activePlayerState();
    QString currentName = valueForKey(player, kKeyCorvetteName, kKeyCorvetteNameLong).toString();
    if (currentName.isEmpty()) currentName = tr("Active Corvette");
    QJsonValue currentSeed = valueForKey(player, kKeyCorvetteSeed, kKeyCorvetteSeedLong);

    bool matchedActive = false;
    for (const CorvetteEntry &entry : localCorvettes_) {
        bool inUse = false;
        if (!currentSeed.isUndefined() && !entry.seed.isUndefined()) {
            inUse = jsonValuesEqual(entry.seed, currentSeed);
        } else if (!entry.name.isEmpty() && entry.name == currentName) {
            inUse = true;
        }
        if (inUse) {
            matchedActive = true;
        }
        QString label = entry.name;
        if (inUse) {
            label = tr("%1 (IN USE)").arg(label);
        }
        int index = corvetteCombo_->count();
        corvetteCombo_->addItem(label, QVariant::fromValue(entry.fileName));
        corvetteCombo_->setItemData(index, inUse, Qt::UserRole + 1);
    }

    if (!matchedActive) {
        corvetteCombo_->insertItem(0, tr("%1 (IN USE)").arg(currentName), QVariant::fromValue(QString("ACTIVE")));
        corvetteCombo_->setItemData(0, true, Qt::UserRole + 1);
    }

    corvetteCombo_->blockSignals(false);
    if (corvetteCombo_->count() > 0) {
        corvetteCombo_->setCurrentIndex(0);
    }
    onCorvetteSelected(corvetteCombo_->currentIndex());
}

void CorvetteManagerPage::onCorvetteSelected(int index)
{
    if (index < 0) {
        useButton_->setEnabled(false);
        return;
    }
    bool inUse = corvetteCombo_->itemData(index, Qt::UserRole + 1).toBool();
    QString data = corvetteCombo_->itemData(index).toString();
    useButton_->setEnabled(!inUse && data != "ACTIVE");
}

void CorvetteManagerPage::onImportClicked()
{
    QString path = QFileDialog::getOpenFileName(this, tr("Import Corvette"), QString(), tr("JSON Files (*.json)"));
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Error"), tr("Unable to open file."));
        return;
    }

    QByteArray data = file.readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject()) {
        QMessageBox::warning(this, tr("Error"), tr("Invalid Corvette JSON."));
        return;
    }

    QString baseName = QFileInfo(path).baseName();
    QString targetPath = QDir(localCorvettesPath()).filePath(baseName + ".json");
    
    QFile targetFile(targetPath);
    if (targetFile.exists()) {
        if (QMessageBox::question(this, tr("Overwrite"), tr("File already exists. Overwrite?")) != QMessageBox::Yes) {
            return;
        }
    }

    if (targetFile.open(QIODevice::WriteOnly)) {
        targetFile.write(data);
        targetFile.close();
        loadLocalCorvettes();
        rebuildCorvetteList();
        emit statusMessage(tr("Imported %1").arg(baseName));
    }
}

void CorvetteManagerPage::onExportClicked()
{
    QJsonObject corvetteData;
    QString name = tr("ActiveCorvette");

    int index = corvetteCombo_->currentIndex();
    QString data = corvetteCombo_->itemData(index).toString();
    if (!data.isEmpty() && data != "ACTIVE") {
        QString localPath = QDir(localCorvettesPath()).filePath(data);
        QFile localFile(localPath);
        if (localFile.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(localFile.readAll());
            if (doc.isObject()) {
                corvetteData = doc.object();
            }
        }
    }

    if (corvetteData.isEmpty()) {
        QJsonObject player = activePlayerState();
        QString resolvedName = valueForKey(player, kKeyCorvetteName, kKeyCorvetteNameLong).toString();
        if (!resolvedName.isEmpty()) {
            name = resolvedName;
        }
        corvetteData.insert(kKeyCorvetteNameLong, name);
        corvetteData.insert(kKeyCorvetteSeedLong, valueForKey(player, kKeyCorvetteSeed, kKeyCorvetteSeedLong));

        QVariantList invPath = corvetteInventoryPath();
        if (!invPath.isEmpty()) {
            corvetteData.insert(kKeyCorvetteInventoryLong, valueAtPath(rootDoc_.object(), invPath));
        }
        QVariantList layoutPath = corvetteLayoutPath();
        if (!layoutPath.isEmpty()) {
            corvetteData.insert(kKeyCorvetteLayoutLong, valueAtPath(rootDoc_.object(), layoutPath));
        }
    } else {
        QString resolvedName = valueForKey(corvetteData, kKeyCorvetteName, kKeyCorvetteNameLong).toString();
        if (!resolvedName.isEmpty()) {
            name = resolvedName;
        }
    }

    QString fileName = name + ".json";
    QString path = QFileDialog::getSaveFileName(this, tr("Export Corvette"), fileName, tr("JSON Files (*.json)"));
    if (path.isEmpty()) return;

    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        QJsonDocument doc(corvetteData);
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
        
        // Also copy to local storage
        QString localPath = QDir(localCorvettesPath()).filePath(QFileInfo(path).fileName());
        QFile::remove(localPath);
        QFile::copy(path, localPath);
        
        loadLocalCorvettes();
        rebuildCorvetteList();
        emit statusMessage(tr("Exported %1").arg(name));
    }
}

void CorvetteManagerPage::onUseClicked()
{
    int index = corvetteCombo_->currentIndex();
    if (index < 0) return;
    if (corvetteCombo_->itemData(index, Qt::UserRole + 1).toBool()) return;

    QString fileName = corvetteCombo_->itemData(index).toString();
    if (fileName == "ACTIVE") return;
    QString path = QDir(localCorvettesPath()).filePath(fileName);

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isNull() || !doc.isObject()) return;

    QJsonObject corvetteData = doc.object();
    
    // Apply fields to save
    QVariantList playerPath = playerBasePath();
    QJsonObject player = activePlayerState();
    QString nameKey = resolveKeyName(player, kKeyCorvetteName, kKeyCorvetteNameLong);
    QString seedKey = resolveKeyName(player, kKeyCorvetteSeed, kKeyCorvetteSeedLong);
    QString invKey = resolveKeyName(player, kKeyCorvetteInventory, kKeyCorvetteInventoryLong);
    QString layoutKey = resolveKeyName(player, kKeyCorvetteLayout, kKeyCorvetteLayoutLong);
    QJsonValue nameValue = valueForKey(corvetteData, kKeyCorvetteName, kKeyCorvetteNameLong);
    QJsonValue seedValue = valueForKey(corvetteData, kKeyCorvetteSeed, kKeyCorvetteSeedLong);
    QJsonValue invValue = valueForKey(corvetteData, kKeyCorvetteInventory, kKeyCorvetteInventoryLong);
    QJsonValue layoutValue = valueForKey(corvetteData, kKeyCorvetteLayout, kKeyCorvetteLayoutLong);

    if (!nameKey.isEmpty() && !nameValue.isUndefined()) {
        applyValueAtPath(playerPath + QVariantList{nameKey}, nameValue);
    }
    if (!seedKey.isEmpty() && !seedValue.isUndefined()) {
        applyValueAtPath(playerPath + QVariantList{seedKey}, seedValue);
    }
    if (!invKey.isEmpty() && !invValue.isUndefined()) {
        applyValueAtPath(playerPath + QVariantList{invKey}, invValue);
    }
    if (!layoutKey.isEmpty() && !layoutValue.isUndefined()) {
        applyValueAtPath(playerPath + QVariantList{layoutKey}, layoutValue);
    }

    refreshGrids();
    hasUnsavedChanges_ = true;
    rebuildCorvetteList();
    emit statusMessage(tr("Copied %1 into save.").arg(corvetteCombo_->currentText()));
}

void CorvetteManagerPage::refreshGrids()
{
    QVariantList invPath = corvetteInventoryPath();
    if (invPath.isEmpty()) {
        return;
    }
    QJsonObject inventory = valueAtPath(rootDoc_.object(), invPath).toObject();
    
    if (inventory.isEmpty()) return;

    QString slotsKey = resolveKeyName(inventory, kKeySlots, kKeySlotsLong);
    QString validKey = resolveKeyName(inventory, kKeyValidSlots, kKeyValidSlotsLong);
    QString specialKey = resolveKeyName(inventory, kKeySpecialSlots, kKeySpecialSlotsLong);
    if (slotsKey.isEmpty() || validKey.isEmpty() || specialKey.isEmpty()) {
        return;
    }

    QJsonArray slotsArray = inventory.value(slotsKey).toArray();
    QJsonArray validSlots = inventory.value(validKey).toArray();
    QJsonArray specialSlots = inventory.value(specialKey).toArray();

    inventoryGrid_->setInventory(tr("Corvette Inventory"), slotsArray, validSlots, specialSlots);
    inventoryGrid_->setCommitHandler([this, invPath, slotsKey, validKey, specialKey](const QJsonArray &s, const QJsonArray &v, const QJsonArray &m) {
        QJsonObject inv = valueAtPath(rootDoc_.object(), invPath).toObject();
        inv.insert(slotsKey, s);
        inv.insert(validKey, v);
        inv.insert(specialKey, m);
        applyValueAtPath(invPath, inv);
        hasUnsavedChanges_ = true;
    });

    // Tech grid if available
    QString techKey = resolveKeyName(inventory, kKeyTechInventory, kKeyTechInventoryLong);
    QJsonValue techVal = techKey.isEmpty() ? QJsonValue() : inventory.value(techKey);
    if (techVal.isObject()) {
        QJsonObject tech = techVal.toObject();
        QString techSlotsKey = resolveKeyName(tech, kKeySlots, kKeySlotsLong);
        QString techValidKey = resolveKeyName(tech, kKeyValidSlots, kKeyValidSlotsLong);
        QString techSpecialKey = resolveKeyName(tech, kKeySpecialSlots, kKeySpecialSlotsLong);
        if (techSlotsKey.isEmpty() || techValidKey.isEmpty() || techSpecialKey.isEmpty()) {
            return;
        }
        QJsonArray tSlots = tech.value(techSlotsKey).toArray();
        QJsonArray tValid = tech.value(techValidKey).toArray();
        QJsonArray tSpecial = tech.value(techSpecialKey).toArray();
        
        techGrid_->setInventory(tr("Corvette Technology"), tSlots, tValid, tSpecial);
        techGrid_->setCommitHandler([this, invPath, techKey, techSlotsKey, techValidKey, techSpecialKey](const QJsonArray &s, const QJsonArray &v, const QJsonArray &m) {
            QJsonObject inv = valueAtPath(rootDoc_.object(), invPath).toObject();
            QJsonObject tech = inv.value(techKey).toObject();
            tech.insert(techSlotsKey, s);
            tech.insert(techValidKey, v);
            tech.insert(techSpecialKey, m);
            inv.insert(techKey, tech);
            applyValueAtPath(invPath, inv);
            hasUnsavedChanges_ = true;
        });
    }
}

QString CorvetteManagerPage::localCorvettesPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/corvettes";
}

QJsonObject CorvetteManagerPage::activePlayerState() const
{
    QJsonObject root = rootDoc_.object();
    QVariantList path = playerStatePathForContext(usingExpeditionContext_);
    if (path.isEmpty()) {
        return QJsonObject();
    }
    return valueAtPath(root, path).toObject();
}

QVariantList CorvetteManagerPage::playerBasePath() const
{
    return playerStatePathForContext(usingExpeditionContext_);
}

QVariantList CorvetteManagerPage::corvetteInventoryPath() const
{
    QJsonObject player = activePlayerState();
    QString invKey = resolveKeyName(player, kKeyCorvetteInventory, kKeyCorvetteInventoryLong);
    if (invKey.isEmpty()) {
        return {};
    }
    QVariantList path = playerBasePath();
    path << invKey;
    return path;
}

QVariantList CorvetteManagerPage::corvetteLayoutPath() const
{
    QJsonObject player = activePlayerState();
    QString layoutKey = resolveKeyName(player, kKeyCorvetteLayout, kKeyCorvetteLayoutLong);
    if (layoutKey.isEmpty()) {
        return {};
    }
    QVariantList path = playerBasePath();
    path << layoutKey;
    return path;
}

QVariantList CorvetteManagerPage::playerStatePathForContext(bool expedition) const
{
    if (expedition) {
        QVariantList shortPath = {kKeyExpeditionContext, "6f="};
        if (valueAtPath(rootDoc_.object(), shortPath).isObject()) {
            return shortPath;
        }
        QVariantList mixedPath = {kKeyExpeditionContext, kKeyPlayerStateLong};
        if (valueAtPath(rootDoc_.object(), mixedPath).isObject()) {
            return mixedPath;
        }
        QVariantList longPath = {kKeyExpeditionContextLong, kKeyPlayerStateLong};
        if (valueAtPath(rootDoc_.object(), longPath).isObject()) {
            return longPath;
        }
        QVariantList longAltPath = {kKeyExpeditionContextLong, "6f="};
        if (valueAtPath(rootDoc_.object(), longAltPath).isObject()) {
            return longAltPath;
        }
        return {};
    }

    QVariantList shortPath = {kKeyPlayerState, "6f="};
    if (valueAtPath(rootDoc_.object(), shortPath).isObject()) {
        return shortPath;
    }
    QVariantList mixedPath = {kKeyPlayerState, kKeyPlayerStateLong};
    if (valueAtPath(rootDoc_.object(), mixedPath).isObject()) {
        return mixedPath;
    }
    QVariantList longPath = {kKeyBaseContextLong, kKeyPlayerStateLong};
    if (valueAtPath(rootDoc_.object(), longPath).isObject()) {
        return longPath;
    }
    QVariantList longAltPath = {kKeyBaseContextLong, "6f="};
    if (valueAtPath(rootDoc_.object(), longAltPath).isObject()) {
        return longAltPath;
    }
    return {};
}

void CorvetteManagerPage::updateActiveContext()
{
    usingExpeditionContext_ = false;
    if (!rootDoc_.isObject()) {
        return;
    }
    QJsonObject root = rootDoc_.object();
    QJsonValue rootValue = root;
    QString context = findMappedKey(rootValue, QStringLiteral("ActiveContext")).toString();
    if (context.isEmpty()) {
        context = root.value(kKeyActiveContext).toString();
    }
    QString normalized = context.trimmed().toLower();
    if (normalized.isEmpty() || normalized == QString(kKeyContextMain).toLower()) {
        return;
    }
    QJsonObject expedition;
    auto it = root.find(kKeyExpeditionContext);
    if (it != root.end()) {
        expedition = it.value().toObject();
    } else {
        expedition = root.value(kKeyExpeditionContextLong).toObject();
    }
    if (expedition.contains("6f=") || expedition.contains(kKeyPlayerStateLong)) {
        usingExpeditionContext_ = true;
    } else {
        QString mappedKey = findTopLevelMappedKeyName(root, QStringLiteral("ExpeditionContext"));
        if (!mappedKey.isEmpty()) {
            QJsonObject mapped = root.value(mappedKey).toObject();
            if (mapped.contains("6f=") || mapped.contains(kKeyPlayerStateLong)) {
                usingExpeditionContext_ = true;
            }
        }
    }
}

bool CorvetteManagerPage::playerHasCorvetteData(bool expedition) const
{
    QVariantList path = playerStatePathForContext(expedition);
    if (path.isEmpty()) {
        return false;
    }
    QJsonObject player = valueAtPath(rootDoc_.object(), path).toObject();
    if (player.isEmpty()) {
        return false;
    }
    QString invKey = resolveKeyName(player, kKeyCorvetteInventory, kKeyCorvetteInventoryLong);
    if (!invKey.isEmpty()) {
        return true;
    }
    QString layoutKey = resolveKeyName(player, kKeyCorvetteLayout, kKeyCorvetteLayoutLong);
    return !layoutKey.isEmpty();
}

QJsonValue CorvetteManagerPage::valueAtPath(const QJsonValue &root, const QVariantList &path) const
{
    QJsonValue current = root;
    for (const QVariant &segment : path) {
        if (segment.canConvert<int>()) {
            current = current.toArray().at(segment.toInt());
        } else {
            current = current.toObject().value(segment.toString());
        }
    }
    return current;
}

void CorvetteManagerPage::applyValueAtPath(const QVariantList &path, const QJsonValue &value)
{
    if (SaveJsonModel::setLosslessValue(losslessDoc_, path, value)) {
        syncRootFromLossless();
        hasUnsavedChanges_ = true;
    }
}

bool CorvetteManagerPage::syncRootFromLossless(QString *errorMessage)
{
    return SaveJsonModel::syncRootFromLossless(losslessDoc_, rootDoc_, errorMessage);
}
