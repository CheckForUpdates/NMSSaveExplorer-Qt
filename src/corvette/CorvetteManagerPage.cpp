#include "corvette/CorvetteManagerPage.h"

#include "core/JsonMapper.h"
#include "core/LosslessJsonDocument.h"
#include "core/ResourceLocator.h"
#include "core/SaveCache.h"
#include "core/SaveEncoder.h"
#include "core/SaveJsonModel.h"
#include "inventory/InventoryGridWidget.h"
#include "registry/LocalizationRegistry.h"

#include <QDir>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QComboBox>
#include <QAbstractSpinBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>
#include <algorithm>

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
const char *kKeyFleetFrigates = ";Du";
const char *kKeyFleetFrigatesLong = "FleetFrigates";
const char *kKeyFleetExpeditions = "kw:";
const char *kKeyFleetExpeditionsLong = "FleetExpeditions";
const char *kKeyActiveFrigateIndices = "sbg";
const char *kKeyActiveFrigateIndicesLong = "ActiveFrigateIndices";
const char *kKeyAllFrigateIndices = "lD@";
const char *kKeyAllFrigateIndicesLong = "AllFrigateIndices";
const char *kKeyFrigateName = "fH8";
const char *kKeyFrigateNameLong = "CustomName";
const char *kKeyFrigateClass = "uw7";
const char *kKeyFrigateClassLong = "FrigateClass";
const char *kKeyInventoryClass = "B@N";
const char *kKeyInventoryClassLong = "InventoryClass";
const char *kKeyInventoryClassValue = "1o6";
const char *kKeyInventoryClassValueLong = "InventoryClass";
const char *kKeyHomeSystemSeed = "@ui";
const char *kKeyHomeSystemSeedLong = "HomeSystemSeed";
const char *kKeyResourceSeed = "SLc";
const char *kKeyResourceSeedLong = "ResourceSeed";
const char *kKeyRace = "SS2";
const char *kKeyRaceLong = "Race";
const char *kKeyAlienRace = "0Hi";
const char *kKeyAlienRaceLong = "AlienRace";
const char *kKeyStats = "gUR";
const char *kKeyStatsLong = "Stats";
const char *kKeyTraits = "Mjm";
const char *kKeyTraitsLong = "TraitIDs";
const char *kKeyTotalExpeditions = "5es";
const char *kKeyTotalExpeditionsLong = "TotalNumberOfExpeditions";
const char *kKeyTimesDamaged = "MuL";
const char *kKeyTimesDamagedLong = "NumberOfTimesDamaged";
const char *kKeySuccessfulEvents = "v=L";
const char *kKeySuccessfulEventsLong = "TotalNumberOfSuccessfulEvents";
const char *kKeyFailedEvents = "5VG";
const char *kKeyFailedEventsLong = "TotalNumberOfFailedEvents";

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

QString seedTextFromValue(const QJsonValue &value)
{
    if (value.isString()) {
        return value.toString();
    }
    if (value.isDouble()) {
        return QString::number(static_cast<qulonglong>(value.toDouble()));
    }
    if (value.isArray()) {
        QJsonArray array = value.toArray();
        if (array.size() >= 2 && array.at(1).isString()) {
            return array.at(1).toString();
        }
        if (!array.isEmpty() && array.at(0).isString()) {
            return array.at(0).toString();
        }
    }
    return value.toVariant().toString();
}

void setSeedValue(QJsonObject &obj, const QString &key, const QString &raw)
{
    if (key.isEmpty()) {
        return;
    }
    QString trimmed = raw.trimmed();
    bool ok = false;
    qulonglong seed = trimmed.startsWith("0x", Qt::CaseInsensitive)
                          ? trimmed.mid(2).toULongLong(&ok, 16)
                          : trimmed.toULongLong(&ok, 10);
    if (!ok) {
        return;
    }
    QString formatted = QStringLiteral("0x%1").arg(QString::number(seed, 16).toUpper());
    QJsonValue current = obj.value(key);
    if (current.isArray()) {
        QJsonArray array = current.toArray();
        if (array.size() < 2) {
            array = QJsonArray{true, formatted};
        } else {
            array[0] = true;
            array[1] = formatted;
        }
        obj.insert(key, array);
        return;
    }
    obj.insert(key, QJsonArray{true, formatted});
}

QString keyForObject(const QJsonObject &obj, const char *shortKey, const char *longKey)
{
    return resolveKeyName(obj, shortKey, longKey);
}

QJsonObject nestedObjectForKey(const QJsonObject &obj, const char *shortKey, const char *longKey)
{
    return valueForKey(obj, shortKey, longKey).toObject();
}

QString nestedEnumValue(const QJsonObject &obj, const char *outerShort, const char *outerLong,
                        const char *innerShort, const char *innerLong)
{
    QJsonObject nested = nestedObjectForKey(obj, outerShort, outerLong);
    if (nested.isEmpty()) {
        return QString();
    }
    return valueForKey(nested, innerShort, innerLong).toString();
}

void setNestedEnumValue(QJsonObject &obj, const char *outerShort, const char *outerLong,
                        const char *innerShort, const char *innerLong, const QString &value)
{
    QString outerKey = keyForObject(obj, outerShort, outerLong);
    if (outerKey.isEmpty()) {
        outerKey = QString::fromUtf8(outerLong);
    }
    QJsonObject nested = obj.value(outerKey).toObject();
    QString innerKey = keyForObject(nested, innerShort, innerLong);
    if (innerKey.isEmpty()) {
        innerKey = QString::fromUtf8(innerLong);
    }
    nested.insert(innerKey, value);
    obj.insert(outerKey, nested);
}

const QStringList kFrigateStatLabels = {
    QStringLiteral("Combat"),
    QStringLiteral("Exploration"),
    QStringLiteral("Mining"),
    QStringLiteral("Diplomatic"),
    QStringLiteral("Fuel Burn Rate"),
    QStringLiteral("Fuel Capacity"),
    QStringLiteral("Speed"),
    QStringLiteral("Extra Loot"),
    QStringLiteral("Repair"),
    QStringLiteral("Invulnerable"),
    QStringLiteral("Stealth")
};

QString traitDisplayText(const QString &traitId)
{
    QString trimmed = traitId.trimmed();
    if (trimmed.isEmpty() || trimmed == QStringLiteral("^")) {
        return QStringLiteral("^");
    }
    QString token = trimmed;
    if (token.startsWith('^')) {
        token = token.mid(1);
    }
    QString resolved = LocalizationRegistry::resolveToken(token);
    if (resolved.isEmpty()) {
        return trimmed;
    }
    return QStringLiteral("%1 (%2)").arg(resolved, trimmed);
}

bool findPathToMappedKey(const QJsonValue &value, const QString &targetLongKey,
                         const QVariantList &prefix, QVariantList *outPath)
{
    if (value.isObject()) {
        QJsonObject obj = value.toObject();
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            const QString key = it.key();
            if (key == targetLongKey || JsonMapper::mapKey(key) == targetLongKey) {
                *outPath = prefix;
                outPath->append(key);
                return true;
            }
            QVariantList next = prefix;
            next.append(key);
            if (findPathToMappedKey(it.value(), targetLongKey, next, outPath)) {
                return true;
            }
        }
    } else if (value.isArray()) {
        QJsonArray arr = value.toArray();
        for (int i = 0; i < arr.size(); ++i) {
            QVariantList next = prefix;
            next.append(i);
            if (findPathToMappedKey(arr.at(i), targetLongKey, next, outPath)) {
                return true;
            }
        }
    }
    return false;
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
    mainLayout->setSpacing(12);

    auto *frigateRow = new QHBoxLayout();
    frigateRow->setSpacing(8);
    frigateRow->addWidget(new QLabel(tr("Select Frigate:"), this));
    frigateCombo_ = new QComboBox(this);
    frigateCombo_->setMinimumWidth(320);
    frigateRow->addWidget(frigateCombo_);
    frigateRow->addStretch();
    mainLayout->addLayout(frigateRow);

    connect(frigateCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CorvetteManagerPage::onFrigateSelected);

    auto *customizationGroup = new QGroupBox(tr("Customization"), this);
    auto *customizationForm = new QFormLayout(customizationGroup);
    customizationForm->setLabelAlignment(Qt::AlignLeft);
    customizationForm->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    customizationForm->setHorizontalSpacing(12);
    customizationForm->setVerticalSpacing(6);

    frigateNameEdit_ = new QLineEdit(this);
    customizationForm->addRow(tr("Name"), frigateNameEdit_);

    frigateInventoryClassCombo_ = new QComboBox(this);
    frigateInventoryClassCombo_->setEditable(true);
    frigateInventoryClassCombo_->addItems({QStringLiteral("S"), QStringLiteral("A"),
                                           QStringLiteral("B"), QStringLiteral("C")});
    customizationForm->addRow(tr("Class"), frigateInventoryClassCombo_);

    frigateClassCombo_ = new QComboBox(this);
    frigateClassCombo_->setEditable(true);
    frigateClassCombo_->addItems({QStringLiteral("Combat"),
                                  QStringLiteral("Exploration"),
                                  QStringLiteral("Industrial"),
                                  QStringLiteral("Diplomacy"),
                                  QStringLiteral("Support"),
                                  QStringLiteral("Pirate"),
                                  QStringLiteral("Normandy"),
                                  QStringLiteral("DeepSpaceCommon")});
    customizationForm->addRow(tr("Type"), frigateClassCombo_);

    frigateHomeSeedEdit_ = new QLineEdit(this);
    customizationForm->addRow(tr("Home Seed"), frigateHomeSeedEdit_);

    frigateResourceSeedEdit_ = new QLineEdit(this);
    customizationForm->addRow(tr("Model Seed"), frigateResourceSeedEdit_);

    frigateRaceCombo_ = new QComboBox(this);
    frigateRaceCombo_->setEditable(true);
    frigateRaceCombo_->addItems({QStringLiteral("Warriors"),
                                 QStringLiteral("Traders"),
                                 QStringLiteral("Explorers"),
                                 QStringLiteral("None")});
    customizationForm->addRow(tr("Crew Race"), frigateRaceCombo_);

    mainLayout->addWidget(customizationGroup);

    auto *statsRow = new QHBoxLayout();
    statsRow->setSpacing(12);
    auto *statsGroup = new QGroupBox(tr("Stats"), this);
    auto *statsForm = new QFormLayout(statsGroup);
    statsForm->setLabelAlignment(Qt::AlignLeft);
    for (const QString &label : kFrigateStatLabels) {
        auto *spin = new QSpinBox(this);
        spin->setRange(-9999, 9999);
        spin->setButtonSymbols(QAbstractSpinBox::PlusMinus);
        statsForm->addRow(label, spin);
        frigateStatSpins_.append(spin);
        connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this,
                &CorvetteManagerPage::onFrigateFieldEdited);
    }
    statsRow->addWidget(statsGroup, 1);

    auto *traitsGroup = new QGroupBox(tr("Traits"), this);
    auto *traitsForm = new QFormLayout(traitsGroup);
    traitsForm->setLabelAlignment(Qt::AlignLeft);
    for (int i = 0; i < 5; ++i) {
        auto *combo = new QComboBox(this);
        combo->setEditable(true);
        combo->setInsertPolicy(QComboBox::NoInsert);
        traitsForm->addRow(tr("Trait %1").arg(i + 1), combo);
        frigateTraitCombos_.append(combo);
        connect(combo, &QComboBox::currentTextChanged, this, &CorvetteManagerPage::onFrigateFieldEdited);
    }
    statsRow->addWidget(traitsGroup, 2);

    auto *expGroup = new QGroupBox(tr("Expeditions"), this);
    auto *expForm = new QFormLayout(expGroup);
    expForm->setLabelAlignment(Qt::AlignLeft);
    frigateTotalExpSpin_ = new QSpinBox(this);
    frigateTotalExpSpin_->setRange(0, 999999);
    expForm->addRow(tr("Expeditions"), frigateTotalExpSpin_);
    frigateTimesDamagedSpin_ = new QSpinBox(this);
    frigateTimesDamagedSpin_->setRange(0, 999999);
    expForm->addRow(tr("Times Damaged"), frigateTimesDamagedSpin_);
    frigateSuccessSpin_ = new QSpinBox(this);
    frigateSuccessSpin_->setRange(0, 999999);
    expForm->addRow(tr("Successful Encounters"), frigateSuccessSpin_);
    frigateFailedSpin_ = new QSpinBox(this);
    frigateFailedSpin_->setRange(0, 999999);
    expForm->addRow(tr("Failed Encounters"), frigateFailedSpin_);

    expForm->addRow(new QLabel(tr("Progress to Next Rank"), this));
    frigateLevelUpInEdit_ = new QLineEdit(this);
    frigateLevelUpInEdit_->setReadOnly(true);
    expForm->addRow(tr("Level Up In"), frigateLevelUpInEdit_);
    frigateLevelUpsRemainingEdit_ = new QLineEdit(this);
    frigateLevelUpsRemainingEdit_->setReadOnly(true);
    expForm->addRow(tr("Level Ups Remaining"), frigateLevelUpsRemainingEdit_);

    expForm->addRow(new QLabel(tr("On Mission"), this));
    frigateMissionStateEdit_ = new QLineEdit(this);
    frigateMissionStateEdit_->setReadOnly(true);
    expForm->addRow(tr("State"), frigateMissionStateEdit_);
    statsRow->addWidget(expGroup, 1);
    mainLayout->addLayout(statsRow);

    connect(frigateNameEdit_, &QLineEdit::editingFinished, this, &CorvetteManagerPage::onFrigateFieldEdited);
    connect(frigateHomeSeedEdit_, &QLineEdit::editingFinished, this, &CorvetteManagerPage::onFrigateFieldEdited);
    connect(frigateResourceSeedEdit_, &QLineEdit::editingFinished, this, &CorvetteManagerPage::onFrigateFieldEdited);
    connect(frigateClassCombo_, &QComboBox::currentTextChanged, this, &CorvetteManagerPage::onFrigateFieldEdited);
    connect(frigateInventoryClassCombo_, &QComboBox::currentTextChanged, this, &CorvetteManagerPage::onFrigateFieldEdited);
    connect(frigateRaceCombo_, &QComboBox::currentTextChanged, this, &CorvetteManagerPage::onFrigateFieldEdited);
    connect(frigateTotalExpSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &CorvetteManagerPage::onFrigateFieldEdited);
    connect(frigateTimesDamagedSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &CorvetteManagerPage::onFrigateFieldEdited);
    connect(frigateSuccessSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &CorvetteManagerPage::onFrigateFieldEdited);
    connect(frigateFailedSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &CorvetteManagerPage::onFrigateFieldEdited);

    // Top selection and action area
    auto *topLayout = new QHBoxLayout();
    topLayout->setSpacing(8);

    auto *label = new QLabel(tr("Template:"), this);
    corvetteCombo_ = new QComboBox(this);
    corvetteCombo_->setMinimumWidth(260);
    connect(corvetteCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CorvetteManagerPage::onCorvetteSelected);

    importButton_ = new QPushButton(tr("Import"), this);
    exportButton_ = new QPushButton(tr("Export"), this);
    useButton_ = new QPushButton(tr("Apply Template"), this);

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

    tabs_->addTab(invScroll, tr("Corvette Inventory"));
    tabs_->addTab(techScroll, tr("Corvette Technology"));

    mainLayout->addWidget(tabs_);
}

bool CorvetteManagerPage::loadFromFile(const QString &filePath, QString *errorMessage)
{
    QByteArray contentBytes;
    QJsonDocument doc;
    std::shared_ptr<LosslessJsonDocument> lossless;
    if (!SaveCache::loadWithLossless(filePath, &contentBytes, &doc, &lossless, errorMessage)) {
        return false;
    }
    return loadFromPrepared(filePath, doc, lossless, errorMessage);
}

bool CorvetteManagerPage::loadFromPrepared(
    const QString &filePath, const QJsonDocument &doc,
    const std::shared_ptr<LosslessJsonDocument> &losslessDoc, QString *errorMessage)
{
    if (!losslessDoc) {
        if (errorMessage) {
            *errorMessage = tr("Failed to load lossless JSON.");
        }
        return false;
    }

    currentFilePath_ = filePath;
    rootDoc_ = doc;
    losslessDoc_ = losslessDoc;

    updateActiveContext();
    if (!playerHasCorvetteData(usingExpeditionContext_) && !playerHasFrigateData(usingExpeditionContext_)) {
        bool alternate = !usingExpeditionContext_;
        if (playerHasCorvetteData(alternate) || playerHasFrigateData(alternate)) {
            usingExpeditionContext_ = alternate;
        }
    }
    loadLocalCorvettes();
    rebuildCorvetteList();
    rebuildFrigateList();
    refreshGrids();
    refreshFrigateEditor();

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

void CorvetteManagerPage::clearLoadedSave()
{
    currentFilePath_.clear();
    rootDoc_ = QJsonDocument();
    losslessDoc_.reset();
    hasUnsavedChanges_ = false;
    usingExpeditionContext_ = false;
    localCorvettes_.clear();
    if (corvetteCombo_) {
        corvetteCombo_->blockSignals(true);
        corvetteCombo_->clear();
        corvetteCombo_->blockSignals(false);
    }
    if (frigateCombo_) {
        frigateCombo_->blockSignals(true);
        frigateCombo_->clear();
        frigateCombo_->blockSignals(false);
    }
    if (inventoryGrid_) {
        inventoryGrid_->setInventory(tr("Corvette Inventory"), QJsonArray(), QJsonArray(), QJsonArray());
    }
    if (techGrid_) {
        techGrid_->setInventory(tr("Corvette Technology"), QJsonArray(), QJsonArray(), QJsonArray());
    }
    refreshFrigateEditor();
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

void CorvetteManagerPage::rebuildFrigateList()
{
    if (!frigateCombo_) {
        return;
    }
    QSignalBlocker blocker(frigateCombo_);
    frigateCombo_->clear();

    QVariantList path = fleetFrigatesPath();
    if (path.isEmpty()) {
        return;
    }
    QJsonArray frigates = valueAtPath(rootDoc_.object(), path).toArray();
    QSet<QString> knownTraits;
    for (const QJsonValue &value : frigates) {
        QJsonArray traits = valueForKey(value.toObject(), kKeyTraits, kKeyTraitsLong).toArray();
        for (const QJsonValue &traitValue : traits) {
            QString traitId = traitValue.toString().trimmed();
            if (!traitId.isEmpty()) {
                knownTraits.insert(traitId);
            }
        }
    }
    QStringList sortedTraits = knownTraits.values();
    std::sort(sortedTraits.begin(), sortedTraits.end(), [](const QString &a, const QString &b) {
        return a.toUpper() < b.toUpper();
    });
    for (QComboBox *combo : frigateTraitCombos_) {
        if (!combo) {
            continue;
        }
        QSignalBlocker comboBlocker(combo);
        combo->clear();
        combo->addItem(QStringLiteral("^"), QStringLiteral("^"));
        for (const QString &traitId : sortedTraits) {
            combo->addItem(traitDisplayText(traitId), traitId);
        }
    }

    for (int i = 0; i < frigates.size(); ++i) {
        QJsonObject frigate = frigates.at(i).toObject();
        QString customName = valueForKey(frigate, kKeyFrigateName, kKeyFrigateNameLong).toString().trimmed();
        QString type = nestedEnumValue(frigate, kKeyFrigateClass, kKeyFrigateClassLong,
                                       kKeyFrigateClass, kKeyFrigateClassLong);
        QString display = customName.isEmpty() ? tr("Frigate %1").arg(i + 1) : customName;
        if (!type.isEmpty()) {
            display = tr("%1 (%2)").arg(display, type);
        }
        frigateCombo_->addItem(display, i);
    }
    if (frigateCombo_->count() > 0) {
        frigateCombo_->setCurrentIndex(0);
    }
}

void CorvetteManagerPage::onFrigateSelected(int index)
{
    Q_UNUSED(index);
    refreshFrigateEditor();
}

void CorvetteManagerPage::refreshFrigateEditor()
{
    if (!frigateCombo_) {
        return;
    }

    updatingFrigateUi_ = true;
    auto clearFields = [this]() {
        if (frigateNameEdit_) frigateNameEdit_->clear();
        if (frigateClassCombo_) frigateClassCombo_->setCurrentText(QString());
        if (frigateInventoryClassCombo_) frigateInventoryClassCombo_->setCurrentText(QString());
        if (frigateHomeSeedEdit_) frigateHomeSeedEdit_->clear();
        if (frigateResourceSeedEdit_) frigateResourceSeedEdit_->clear();
        if (frigateRaceCombo_) frigateRaceCombo_->setCurrentText(QString());
        for (QSpinBox *spin : frigateStatSpins_) {
            if (spin) spin->setValue(0);
        }
        for (QComboBox *combo : frigateTraitCombos_) {
            if (combo) combo->setCurrentText(QString());
        }
        if (frigateTotalExpSpin_) frigateTotalExpSpin_->setValue(0);
        if (frigateTimesDamagedSpin_) frigateTimesDamagedSpin_->setValue(0);
        if (frigateSuccessSpin_) frigateSuccessSpin_->setValue(0);
        if (frigateFailedSpin_) frigateFailedSpin_->setValue(0);
        if (frigateLevelUpInEdit_) frigateLevelUpInEdit_->clear();
        if (frigateLevelUpsRemainingEdit_) frigateLevelUpsRemainingEdit_->clear();
        if (frigateMissionStateEdit_) frigateMissionStateEdit_->clear();
    };

    const int comboIndex = frigateCombo_->currentIndex();
    const int frigateIndex = frigateCombo_->itemData(comboIndex).toInt();
    QVariantList path = fleetFrigatesPath();
    if (comboIndex < 0 || path.isEmpty()) {
        clearFields();
        updatingFrigateUi_ = false;
        return;
    }

    QJsonArray frigates = valueAtPath(rootDoc_.object(), path).toArray();
    if (frigateIndex < 0 || frigateIndex >= frigates.size()) {
        clearFields();
        updatingFrigateUi_ = false;
        return;
    }

    QJsonObject frigate = frigates.at(frigateIndex).toObject();
    frigateNameEdit_->setText(valueForKey(frigate, kKeyFrigateName, kKeyFrigateNameLong).toString());
    frigateClassCombo_->setCurrentText(nestedEnumValue(frigate, kKeyFrigateClass, kKeyFrigateClassLong,
                                                       kKeyFrigateClass, kKeyFrigateClassLong));
    frigateInventoryClassCombo_->setCurrentText(nestedEnumValue(frigate, kKeyInventoryClass, kKeyInventoryClassLong,
                                                                kKeyInventoryClassValue, kKeyInventoryClassValueLong));
    frigateHomeSeedEdit_->setText(seedTextFromValue(valueForKey(frigate, kKeyHomeSystemSeed, kKeyHomeSystemSeedLong)));
    frigateResourceSeedEdit_->setText(seedTextFromValue(valueForKey(frigate, kKeyResourceSeed, kKeyResourceSeedLong)));
    frigateRaceCombo_->setCurrentText(nestedEnumValue(frigate, kKeyRace, kKeyRaceLong,
                                                      kKeyAlienRace, kKeyAlienRaceLong));

    QJsonArray stats = valueForKey(frigate, kKeyStats, kKeyStatsLong).toArray();
    for (int i = 0; i < frigateStatSpins_.size(); ++i) {
        int value = 0;
        if (i < stats.size()) {
            value = stats.at(i).toInt();
        }
        frigateStatSpins_.at(i)->setValue(value);
    }

    QJsonArray traits = valueForKey(frigate, kKeyTraits, kKeyTraitsLong).toArray();
    for (int i = 0; i < frigateTraitCombos_.size(); ++i) {
        QString value;
        if (i < traits.size()) {
            value = traits.at(i).toString();
        }
        QComboBox *combo = frigateTraitCombos_.at(i);
        int matchIndex = combo->findData(value);
        if (matchIndex >= 0) {
            combo->setCurrentIndex(matchIndex);
        } else {
            combo->setCurrentText(value);
        }
    }

    frigateTotalExpSpin_->setValue(valueForKey(frigate, kKeyTotalExpeditions, kKeyTotalExpeditionsLong).toInt());
    frigateTimesDamagedSpin_->setValue(valueForKey(frigate, kKeyTimesDamaged, kKeyTimesDamagedLong).toInt());
    frigateSuccessSpin_->setValue(valueForKey(frigate, kKeySuccessfulEvents, kKeySuccessfulEventsLong).toInt());
    frigateFailedSpin_->setValue(valueForKey(frigate, kKeyFailedEvents, kKeyFailedEventsLong).toInt());
    refreshFrigateProgressFields(frigate);

    updatingFrigateUi_ = false;
}

void CorvetteManagerPage::refreshFrigateProgressFields(const QJsonObject &frigate)
{
    if (!frigateLevelUpInEdit_ || !frigateLevelUpsRemainingEdit_ || !frigateMissionStateEdit_) {
        return;
    }
    const int totalExp = valueForKey(frigate, kKeyTotalExpeditions, kKeyTotalExpeditionsLong).toInt();
    const int missionsForRankUp = 5;
    const int maxRankMissions = 55;
    int levelUpIn = missionsForRankUp - (totalExp % missionsForRankUp);
    if (levelUpIn <= 0) {
        levelUpIn = missionsForRankUp;
    }
    int remaining = 0;
    if (totalExp < maxRankMissions) {
        remaining = (maxRankMissions - totalExp + missionsForRankUp - 1) / missionsForRankUp;
    }
    frigateLevelUpInEdit_->setText(QString::number(levelUpIn));
    frigateLevelUpsRemainingEdit_->setText(QString::number(remaining));

    int currentIndex = frigateCombo_ ? frigateCombo_->currentIndex() : -1;
    int frigateIndex = currentIndex >= 0 ? frigateCombo_->itemData(currentIndex).toInt() : -1;
    frigateMissionStateEdit_->setText(frigateIsOnMission(frigateIndex) ? tr("On Mission") : tr("Idle"));
}

void CorvetteManagerPage::onFrigateFieldEdited()
{
    if (updatingFrigateUi_) {
        return;
    }
    if (!frigateCombo_ || frigateCombo_->currentIndex() < 0) {
        return;
    }

    const int frigateIndex = frigateCombo_->itemData(frigateCombo_->currentIndex()).toInt();
    updateFrigateAtIndex(frigateIndex, [this](QJsonObject &frigate) {
        if (frigateNameEdit_) {
            QString key = keyForObject(frigate, kKeyFrigateName, kKeyFrigateNameLong);
            if (key.isEmpty()) key = QString::fromUtf8(kKeyFrigateNameLong);
            frigate.insert(key, frigateNameEdit_->text().trimmed());
        }
        setNestedEnumValue(frigate, kKeyFrigateClass, kKeyFrigateClassLong,
                           kKeyFrigateClass, kKeyFrigateClassLong,
                           frigateClassCombo_ ? frigateClassCombo_->currentText().trimmed() : QString());
        setNestedEnumValue(frigate, kKeyInventoryClass, kKeyInventoryClassLong,
                           kKeyInventoryClassValue, kKeyInventoryClassValueLong,
                           frigateInventoryClassCombo_ ? frigateInventoryClassCombo_->currentText().trimmed() : QString());
        setNestedEnumValue(frigate, kKeyRace, kKeyRaceLong,
                           kKeyAlienRace, kKeyAlienRaceLong,
                           frigateRaceCombo_ ? frigateRaceCombo_->currentText().trimmed() : QString());

        QString homeKey = keyForObject(frigate, kKeyHomeSystemSeed, kKeyHomeSystemSeedLong);
        if (homeKey.isEmpty()) homeKey = QString::fromUtf8(kKeyHomeSystemSeedLong);
        if (frigateHomeSeedEdit_) {
            setSeedValue(frigate, homeKey, frigateHomeSeedEdit_->text());
        }
        QString resourceKey = keyForObject(frigate, kKeyResourceSeed, kKeyResourceSeedLong);
        if (resourceKey.isEmpty()) resourceKey = QString::fromUtf8(kKeyResourceSeedLong);
        if (frigateResourceSeedEdit_) {
            setSeedValue(frigate, resourceKey, frigateResourceSeedEdit_->text());
        }

        QString statsKey = keyForObject(frigate, kKeyStats, kKeyStatsLong);
        if (statsKey.isEmpty()) statsKey = QString::fromUtf8(kKeyStatsLong);
        QJsonArray stats;
        for (QSpinBox *spin : frigateStatSpins_) {
            stats.append(spin ? spin->value() : 0);
        }
        frigate.insert(statsKey, stats);

        QString traitsKey = keyForObject(frigate, kKeyTraits, kKeyTraitsLong);
        if (traitsKey.isEmpty()) traitsKey = QString::fromUtf8(kKeyTraitsLong);
        QJsonArray traits;
        for (QComboBox *combo : frigateTraitCombos_) {
            QString value = combo ? combo->currentData().toString() : QString();
            if (value.isEmpty() && combo) {
                value = combo->currentText().trimmed();
            }
            traits.append(value.isEmpty() ? QStringLiteral("^") : value);
        }
        frigate.insert(traitsKey, traits);

        QString totalExpKey = keyForObject(frigate, kKeyTotalExpeditions, kKeyTotalExpeditionsLong);
        if (totalExpKey.isEmpty()) totalExpKey = QString::fromUtf8(kKeyTotalExpeditionsLong);
        frigate.insert(totalExpKey, frigateTotalExpSpin_ ? frigateTotalExpSpin_->value() : 0);

        QString timesDamagedKey = keyForObject(frigate, kKeyTimesDamaged, kKeyTimesDamagedLong);
        if (timesDamagedKey.isEmpty()) timesDamagedKey = QString::fromUtf8(kKeyTimesDamagedLong);
        frigate.insert(timesDamagedKey, frigateTimesDamagedSpin_ ? frigateTimesDamagedSpin_->value() : 0);

        QString successKey = keyForObject(frigate, kKeySuccessfulEvents, kKeySuccessfulEventsLong);
        if (successKey.isEmpty()) successKey = QString::fromUtf8(kKeySuccessfulEventsLong);
        frigate.insert(successKey, frigateSuccessSpin_ ? frigateSuccessSpin_->value() : 0);

        QString failedKey = keyForObject(frigate, kKeyFailedEvents, kKeyFailedEventsLong);
        if (failedKey.isEmpty()) failedKey = QString::fromUtf8(kKeyFailedEventsLong);
        frigate.insert(failedKey, frigateFailedSpin_ ? frigateFailedSpin_->value() : 0);
    });

    rebuildFrigateList();
    if (frigateCombo_ && frigateCombo_->count() > 0) {
        int safeIndex = qBound(0, frigateIndex, frigateCombo_->count() - 1);
        frigateCombo_->setCurrentIndex(safeIndex);
    }
    refreshFrigateEditor();
}

void CorvetteManagerPage::updateFrigateAtIndex(int frigateIndex, const std::function<void(QJsonObject &)> &mutator)
{
    QVariantList path = fleetFrigatesPath();
    if (path.isEmpty() || frigateIndex < 0) {
        return;
    }
    QJsonArray frigates = valueAtPath(rootDoc_.object(), path).toArray();
    if (frigateIndex >= frigates.size()) {
        return;
    }
    QJsonObject frigate = frigates.at(frigateIndex).toObject();
    mutator(frigate);
    frigates[frigateIndex] = frigate;
    applyValueAtPath(path, frigates);
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
    rebuildFrigateList();
    refreshFrigateEditor();
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

QVariantList CorvetteManagerPage::fleetFrigatesPath() const
{
    auto resolvePath = [this](bool expedition) -> QVariantList {
        QVariantList basePath = playerStatePathForContext(expedition);
        if (basePath.isEmpty()) {
            return {};
        }
        QJsonObject player = valueAtPath(rootDoc_.object(), basePath).toObject();
        QString key = resolveKeyName(player, kKeyFleetFrigates, kKeyFleetFrigatesLong);
        if (key.isEmpty()) {
            return {};
        }
        QVariantList out = basePath;
        out << key;
        return out;
    };

    QVariantList preferred = resolvePath(usingExpeditionContext_);
    if (!preferred.isEmpty()) {
        return preferred;
    }
    QVariantList alternate = resolvePath(!usingExpeditionContext_);
    if (!alternate.isEmpty()) {
        return alternate;
    }
    QVariantList discovered;
    if (findPathToMappedKey(rootDoc_.object(), QStringLiteral("FleetFrigates"), {}, &discovered)) {
        return discovered;
    }
    return {};
}

QVariantList CorvetteManagerPage::fleetExpeditionsPath() const
{
    auto resolvePath = [this](bool expedition) -> QVariantList {
        QVariantList basePath = playerStatePathForContext(expedition);
        if (basePath.isEmpty()) {
            return {};
        }
        QJsonObject player = valueAtPath(rootDoc_.object(), basePath).toObject();
        QString key = resolveKeyName(player, kKeyFleetExpeditions, kKeyFleetExpeditionsLong);
        if (key.isEmpty()) {
            return {};
        }
        QVariantList out = basePath;
        out << key;
        return out;
    };

    QVariantList preferred = resolvePath(usingExpeditionContext_);
    if (!preferred.isEmpty()) {
        return preferred;
    }
    QVariantList alternate = resolvePath(!usingExpeditionContext_);
    if (!alternate.isEmpty()) {
        return alternate;
    }
    QVariantList discovered;
    if (findPathToMappedKey(rootDoc_.object(), QStringLiteral("FleetExpeditions"), {}, &discovered)) {
        return discovered;
    }
    return {};
}

QVariantList CorvetteManagerPage::playerStatePathForContext(bool expedition) const
{
    auto isObjectAtPath = [this](const QVariantList &path) {
        return valueAtPath(rootDoc_.object(), path).isObject();
    };

    QString mappedExpeditionContext = findTopLevelMappedKeyName(rootDoc_.object(), QStringLiteral("ExpeditionContext"));
    QString mappedBaseContext = findTopLevelMappedKeyName(rootDoc_.object(), QStringLiteral("BaseContext"));
    QString mappedPlayerState = findTopLevelMappedKeyName(rootDoc_.object(), QStringLiteral("PlayerStateData"));

    QList<QVariantList> candidates;
    if (expedition) {
        candidates << QVariantList{kKeyExpeditionContext, "6f="}
                   << QVariantList{kKeyExpeditionContext, kKeyPlayerStateLong}
                   << QVariantList{kKeyExpeditionContext}
                   << QVariantList{kKeyExpeditionContextLong, kKeyPlayerStateLong}
                   << QVariantList{kKeyExpeditionContextLong, "6f="}
                   << QVariantList{kKeyExpeditionContextLong};
        if (!mappedExpeditionContext.isEmpty()) {
            candidates << QVariantList{mappedExpeditionContext, "6f="}
                       << QVariantList{mappedExpeditionContext, kKeyPlayerStateLong}
                       << QVariantList{mappedExpeditionContext};
            if (!mappedPlayerState.isEmpty()) {
                candidates << QVariantList{mappedExpeditionContext, mappedPlayerState};
            }
        }
    } else {
        candidates << QVariantList{kKeyPlayerState, "6f="}
                   << QVariantList{kKeyPlayerState, kKeyPlayerStateLong}
                   << QVariantList{kKeyPlayerState}
                   << QVariantList{kKeyBaseContextLong, kKeyPlayerStateLong}
                   << QVariantList{kKeyBaseContextLong, "6f="}
                   << QVariantList{kKeyBaseContextLong};
        if (!mappedBaseContext.isEmpty()) {
            candidates << QVariantList{mappedBaseContext, "6f="}
                       << QVariantList{mappedBaseContext, kKeyPlayerStateLong}
                       << QVariantList{mappedBaseContext};
            if (!mappedPlayerState.isEmpty()) {
                candidates << QVariantList{mappedBaseContext, mappedPlayerState};
            }
        }
        if (!mappedPlayerState.isEmpty()) {
            candidates << QVariantList{mappedPlayerState};
        }
    }

    for (const QVariantList &path : candidates) {
        if (isObjectAtPath(path)) {
            return path;
        }
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

bool CorvetteManagerPage::playerHasFrigateData(bool expedition) const
{
    QVariantList path = playerStatePathForContext(expedition);
    if (path.isEmpty()) {
        return false;
    }
    QJsonObject player = valueAtPath(rootDoc_.object(), path).toObject();
    if (player.isEmpty()) {
        return false;
    }
    QString frigatesKey = resolveKeyName(player, kKeyFleetFrigates, kKeyFleetFrigatesLong);
    return !frigatesKey.isEmpty();
}

bool CorvetteManagerPage::frigateIsOnMission(int frigateIndex) const
{
    if (frigateIndex < 0) {
        return false;
    }
    QVariantList expeditionsPath = fleetExpeditionsPath();
    if (expeditionsPath.isEmpty()) {
        return false;
    }
    QJsonArray expeditions = valueAtPath(rootDoc_.object(), expeditionsPath).toArray();
    for (const QJsonValue &value : expeditions) {
        QJsonObject expedition = value.toObject();
        QJsonArray activeIndices = valueForKey(expedition, kKeyActiveFrigateIndices,
                                               kKeyActiveFrigateIndicesLong).toArray();
        for (const QJsonValue &idxValue : activeIndices) {
            if (idxValue.toInt(-1) == frigateIndex) {
                return true;
            }
        }
        QJsonArray allIndices = valueForKey(expedition, kKeyAllFrigateIndices,
                                            kKeyAllFrigateIndicesLong).toArray();
        for (const QJsonValue &idxValue : allIndices) {
            if (idxValue.toInt(-1) == frigateIndex) {
                return true;
            }
        }
    }
    return false;
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
