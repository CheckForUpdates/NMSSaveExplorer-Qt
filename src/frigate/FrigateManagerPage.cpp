#include "frigate/FrigateManagerPage.h"

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

// FrigateTemplate markers from mapping.json
const char *kKeyFrigateTemplateInventory = "wem"; // CorvetteStorageInventory
const char *kKeyFrigateTemplateLayout = "9i?";    // CorvetteStorageLayout
const char *kKeyFrigateTemplateName = "tVi";      // CorvetteEditShipName
const char *kKeyFrigateTemplateSeed = "60t";      // CorvetteDraftShipSeed
const char *kKeyTechInventory = "PMT";     // Inventory_TechOnly (general)
const char *kKeyFrigateTemplateInventoryLong = "CorvetteStorageInventory";
const char *kKeyFrigateTemplateLayoutLong = "CorvetteStorageLayout";
const char *kKeyFrigateTemplateNameLong = "CorvetteEditShipName";
const char *kKeyFrigateTemplateSeedLong = "CorvetteDraftShipSeed";
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

FrigateManagerPage::FrigateManagerPage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

void FrigateManagerPage::buildUi()
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
            this, &FrigateManagerPage::onFrigateSelected);

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
                &FrigateManagerPage::onFrigateFieldEdited);
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
        connect(combo, &QComboBox::currentTextChanged, this, &FrigateManagerPage::onFrigateFieldEdited);
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

    connect(frigateNameEdit_, &QLineEdit::editingFinished, this, &FrigateManagerPage::onFrigateFieldEdited);
    connect(frigateHomeSeedEdit_, &QLineEdit::editingFinished, this, &FrigateManagerPage::onFrigateFieldEdited);
    connect(frigateResourceSeedEdit_, &QLineEdit::editingFinished, this, &FrigateManagerPage::onFrigateFieldEdited);
    connect(frigateClassCombo_, &QComboBox::currentTextChanged, this, &FrigateManagerPage::onFrigateFieldEdited);
    connect(frigateInventoryClassCombo_, &QComboBox::currentTextChanged, this, &FrigateManagerPage::onFrigateFieldEdited);
    connect(frigateRaceCombo_, &QComboBox::currentTextChanged, this, &FrigateManagerPage::onFrigateFieldEdited);
    connect(frigateTotalExpSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &FrigateManagerPage::onFrigateFieldEdited);
    connect(frigateTimesDamagedSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &FrigateManagerPage::onFrigateFieldEdited);
    connect(frigateSuccessSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &FrigateManagerPage::onFrigateFieldEdited);
    connect(frigateFailedSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &FrigateManagerPage::onFrigateFieldEdited);


}

bool FrigateManagerPage::loadFromFile(const QString &filePath, QString *errorMessage)
{
    QByteArray contentBytes;
    QJsonDocument doc;
    std::shared_ptr<LosslessJsonDocument> lossless;
    if (!SaveCache::loadWithLossless(filePath, &contentBytes, &doc, &lossless, errorMessage)) {
        return false;
    }
    return loadFromPrepared(filePath, doc, lossless, errorMessage);
}

bool FrigateManagerPage::loadFromPrepared(
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
    if (!playerHasFrigateData(usingExpeditionContext_)) {
        bool alternate = !usingExpeditionContext_;
        if (playerHasFrigateData(alternate)) {
            usingExpeditionContext_ = alternate;
        }
    }
    rebuildFrigateList();
    refreshFrigateEditor();

    hasUnsavedChanges_ = false;
    return true;
}

bool FrigateManagerPage::saveChanges(QString *errorMessage)
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

void FrigateManagerPage::clearLoadedSave()
{
    currentFilePath_.clear();
    rootDoc_ = QJsonDocument();
    losslessDoc_.reset();
    hasUnsavedChanges_ = false;
    usingExpeditionContext_ = false;
    if (frigateCombo_) {
        frigateCombo_->blockSignals(true);
        frigateCombo_->clear();
        frigateCombo_->blockSignals(false);
    }
    refreshFrigateEditor();
}



void FrigateManagerPage::rebuildFrigateList()
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

void FrigateManagerPage::onFrigateSelected(int index)
{
    Q_UNUSED(index);
    refreshFrigateEditor();
}

void FrigateManagerPage::refreshFrigateEditor()
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

void FrigateManagerPage::refreshFrigateProgressFields(const QJsonObject &frigate)
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

void FrigateManagerPage::onFrigateFieldEdited()
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

void FrigateManagerPage::updateFrigateAtIndex(int frigateIndex, const std::function<void(QJsonObject &)> &mutator)
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



QJsonObject FrigateManagerPage::activePlayerState() const
{
    QJsonObject root = rootDoc_.object();
    QVariantList path = playerStatePathForContext(usingExpeditionContext_);
    if (path.isEmpty()) {
        return QJsonObject();
    }
    return valueAtPath(root, path).toObject();
}

QVariantList FrigateManagerPage::playerBasePath() const
{
    return playerStatePathForContext(usingExpeditionContext_);
}



QVariantList FrigateManagerPage::fleetFrigatesPath() const
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

QVariantList FrigateManagerPage::fleetExpeditionsPath() const
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

QVariantList FrigateManagerPage::playerStatePathForContext(bool expedition) const
{
    auto isObjectAtPath = [this](const QVariantList &path) {
        return valueAtPath(rootDoc_.object(), path).isObject();
    };

    QString mappedExpeditionContext = findTopLevelMappedKeyName(rootDoc_.object(), QStringLiteral("ExpeditionContext"));
    QString mappedBaseContext = findTopLevelMappedKeyName(rootDoc_.object(), QStringLiteral("BaseContext"));
    QString mappedPlayerState = findTopLevelMappedKeyName(rootDoc_.object(), QStringLiteral("PlayerStateData"));

    QList<QVariantList> candidates;
    if (expedition) {
        candidates << QVariantList{QString::fromUtf8(kKeyExpeditionContext), QStringLiteral("6f=")}
                   << QVariantList{QString::fromUtf8(kKeyExpeditionContext), QString::fromUtf8(kKeyPlayerStateLong)}
                   << QVariantList{QString::fromUtf8(kKeyExpeditionContext)}
                   << QVariantList{QString::fromUtf8(kKeyExpeditionContextLong), QString::fromUtf8(kKeyPlayerStateLong)}
                   << QVariantList{QString::fromUtf8(kKeyExpeditionContextLong), QStringLiteral("6f=")}
                   << QVariantList{QString::fromUtf8(kKeyExpeditionContextLong)};
        if (!mappedExpeditionContext.isEmpty()) {
            candidates << QVariantList{mappedExpeditionContext, QStringLiteral("6f=")}
                       << QVariantList{mappedExpeditionContext, QString::fromUtf8(kKeyPlayerStateLong)}
                       << QVariantList{mappedExpeditionContext};
            if (!mappedPlayerState.isEmpty()) {
                candidates << QVariantList{mappedExpeditionContext, mappedPlayerState};
            }
        }
    } else {
        candidates << QVariantList{QString::fromUtf8(kKeyPlayerState), QStringLiteral("6f=")}
                   << QVariantList{QString::fromUtf8(kKeyPlayerState), QString::fromUtf8(kKeyPlayerStateLong)}
                   << QVariantList{QString::fromUtf8(kKeyPlayerState)}
                   << QVariantList{QString::fromUtf8(kKeyBaseContextLong), QString::fromUtf8(kKeyPlayerStateLong)}
                   << QVariantList{QString::fromUtf8(kKeyBaseContextLong), QStringLiteral("6f=")}
                   << QVariantList{QString::fromUtf8(kKeyBaseContextLong)};
        if (!mappedBaseContext.isEmpty()) {
            candidates << QVariantList{mappedBaseContext, QStringLiteral("6f=")}
                       << QVariantList{mappedBaseContext, QString::fromUtf8(kKeyPlayerStateLong)}
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

void FrigateManagerPage::updateActiveContext()
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



bool FrigateManagerPage::playerHasFrigateData(bool expedition) const
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

bool FrigateManagerPage::frigateIsOnMission(int frigateIndex) const
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

QJsonValue FrigateManagerPage::valueAtPath(const QJsonValue &root, const QVariantList &path) const
{
    QJsonValue current = root;
    for (const QVariant &segment : path) {
        int segmentType = segment.userType();
        if (segmentType == QMetaType::Int || segmentType == QMetaType::UInt || segmentType == QMetaType::LongLong || segmentType == QMetaType::ULongLong) {
            current = current.toArray().at(segment.toInt());
        } else {
            current = current.toObject().value(segment.toString());
        }
    }
    return current;
}

void FrigateManagerPage::applyValueAtPath(const QVariantList &path, const QJsonValue &value)
{
    if (SaveJsonModel::setLosslessValue(losslessDoc_, path, value)) {
        syncRootFromLossless();
        hasUnsavedChanges_ = true;
    }
}

bool FrigateManagerPage::syncRootFromLossless(QString *errorMessage)
{
    return SaveJsonModel::syncRootFromLossless(losslessDoc_, rootDoc_, errorMessage);
}
