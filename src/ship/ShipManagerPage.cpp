#include "ship/ShipManagerPage.h"

#include "core/JsonMapper.h"
#include "core/LosslessJsonDocument.h"
#include "core/ResourceLocator.h"
#include "core/SaveCache.h"
#include "core/SaveEncoder.h"
#include "core/SaveJsonModel.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QFile>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QVBoxLayout>
#include <QJsonParseError>

namespace {
const char *kMappingFile = "mapping.json";
const char *kKeyActiveContext = "XTp";
const char *kKeyExpeditionContext = "2YS";
const char *kKeyPlayerState = "vLc";
const char *kKeyContextMain = "Main";

const char *kKeyActiveContextLong = "ActiveContext";
const char *kKeyExpeditionContextLong = "ExpeditionContext";
const char *kKeyBaseContextLong = "BaseContext";
const char *kKeyPlayerStateLong = "PlayerStateData";

const char *kKeyShipOwnership = "@Cs";
const char *kKeyShipOwnershipLong = "ShipOwnership";
const char *kKeyShipName = "NKm";
const char *kKeyShipNameLong = "Name";
const char *kKeyResource = "NTx";
const char *kKeyResourceLong = "Resource";
const char *kKeyShipResource = ":dY";
const char *kKeyShipResourceLong = "ShipResource";
const char *kKeyCurrentShip = "oJJ";
const char *kKeyCurrentShipLong = "CurrentShip";
const char *kKeyFilename = "93M";
const char *kKeyFilenameLong = "Filename";
const char *kKeySeed = "@EL";
const char *kKeySeedLong = "Seed";
const char *kKeyUseLegacyColours = "J<o";
const char *kKeyUseLegacyColoursLong = "UseLegacyColours";
const char *kKeyUsesLegacyColours = "U>8";
const char *kKeyUsesLegacyColoursLong = "UsesLegacyColours";

const char *kKeyInventory = ";l5";
const char *kKeyInventoryLong = "Inventory";
const char *kKeyInventoryCargo = "gan";
const char *kKeyInventoryCargoLong = "Inventory_Cargo";
const char *kKeyInventoryTech = "PMT";
const char *kKeyInventoryTechLong = "Inventory_TechOnly";
const char *kKeyInventoryClass = "B@N";
const char *kKeyInventoryClassLong = "Class";
const char *kKeyInventoryClassValue = "1o6";
const char *kKeyInventoryClassValueLong = "InventoryClass";
const char *kKeyBaseStatValues = "@bB";
const char *kKeyBaseStatValuesLong = "BaseStatValues";
const char *kKeyBaseStatId = "QL1";
const char *kKeyBaseStatIdLong = "BaseStatID";
const char *kKeyBaseStatValue = ">MX";
const char *kKeyBaseStatValueLong = "Value";

const char *kKeyShipHealth = "ShipHealth";
const char *kKeyShipHealthShort = "KCM";
const char *kKeyShipHealthLegacyShort = "8yM";
const char *kKeyShipShield = "ShipShield";
const char *kKeyShipShieldShort = "NE3";
const char *kKeyShipShieldLegacyShort = "6!S";
const char *kKeyShipShieldLegacy = "Shield";

const char *kStatShipDamage = "^SHIP_DAMAGE";
const char *kStatShipShield = "^SHIP_SHIELD";
const char *kStatShipHyperdrive = "^SHIP_HYPERDRIVE";
const char *kStatShipAgile = "^SHIP_AGILE";

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
        auto it = obj.find(key);
        if (it != obj.end()) {
            return it.value();
        }
        ensureMappingLoaded();
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

QJsonObject findTopLevelMappedObject(const QJsonObject &root, const QString &key)
{
    auto it = root.find(key);
    if (it != root.end()) {
        return it.value().toObject();
    }
    ensureMappingLoaded();
    for (auto it = root.begin(); it != root.end(); ++it) {
        if (JsonMapper::mapKey(it.key()) == key) {
            return it.value().toObject();
        }
    }
    return QJsonObject();
}

QString findTopLevelMappedKeyName(const QJsonObject &root, const QString &key)
{
    auto it = root.find(key);
    if (it != root.end()) {
        return it.key();
    }
    ensureMappingLoaded();
    for (auto it = root.begin(); it != root.end(); ++it) {
        if (JsonMapper::mapKey(it.key()) == key) {
            return it.key();
        }
    }
    return QString();
}

QString filenameForType(const QString &type)
{
    if (type == "Fighter") {
        return QStringLiteral("MODELS/COMMON/SPACECRAFT/FIGHTERS/FIGHTER_PROC.SCENE.MBIN");
    }
    if (type == "Shuttle") {
        return QStringLiteral("MODELS/COMMON/SPACECRAFT/SHUTTLE/SHUTTLE_PROC.SCENE.MBIN");
    }
    if (type == "Hauler") {
        return QStringLiteral("MODELS/COMMON/SPACECRAFT/DROPSHIPS/DROPSHIP_PROC.SCENE.MBIN");
    }
    if (type == "Explorer") {
        return QStringLiteral("MODELS/COMMON/SPACECRAFT/SCIENTIFIC/SCIENTIFIC_PROC.SCENE.MBIN");
    }
    if (type == "Exotic") {
        return QStringLiteral("MODELS/COMMON/SPACECRAFT/ROYAL/ROYAL_PROC.SCENE.MBIN");
    }
    if (type == "Solar") {
        return QStringLiteral("MODELS/COMMON/SPACECRAFT/SAILSHIP/SAILSHIP_PROC.SCENE.MBIN");
    }
    if (type == "Interceptor") {
        return QStringLiteral("MODELS/COMMON/SPACECRAFT/SENTINELSHIP/SENTINELSHIP_PROC.SCENE.MBIN");
    }
    if (type == "Living") {
        return QStringLiteral("MODELS/COMMON/SPACECRAFT/ALIEN/ALIENSHIP_PROC.SCENE.MBIN");
    }
    return QString();
}

QString typeFromFilename(const QString &filename)
{
    QString upper = filename.toUpper();
    if (upper.contains("FIGHTER")) return QStringLiteral("Fighter");
    if (upper.contains("SHUTTLE")) return QStringLiteral("Shuttle");
    if (upper.contains("DROPSHIP")) return QStringLiteral("Hauler");
    if (upper.contains("SCIENTIFIC")) return QStringLiteral("Explorer");
    if (upper.contains("ROYAL")) return QStringLiteral("Exotic");
    if (upper.contains("SAILSHIP")) return QStringLiteral("Solar");
    if (upper.contains("SENTINELSHIP")) return QStringLiteral("Interceptor");
    if (upper.contains("ALIENSHIP")) return QStringLiteral("Living");
    return QString();
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

QString formattedSeedHex(qulonglong seed)
{
    return QStringLiteral("0x%1").arg(QString::number(seed, 16).toUpper());
}

QJsonObject resourceObjectFromShip(const QJsonObject &ship)
{
    QJsonObject resource = ship.value(kKeyResourceLong).toObject();
    if (resource.isEmpty()) {
        resource = ship.value(kKeyResource).toObject();
    }
    return resource;
}

QString resourceFilename(const QJsonObject &resource)
{
    QString filename = resource.value(kKeyFilenameLong).toString();
    if (filename.isEmpty()) {
        filename = resource.value(kKeyFilename).toString();
    }
    return filename.trimmed();
}

QString resourceSeedText(const QJsonObject &resource)
{
    QJsonValue value = resource.value(kKeySeedLong);
    if (value.isUndefined()) {
        value = resource.value(kKeySeed);
    }
    return seedTextFromValue(value).trimmed();
}

bool resourceMatches(const QJsonObject &candidate, const QJsonObject &reference)
{
    QString refFilename = resourceFilename(reference);
    QString refSeed = resourceSeedText(reference);
    if (refFilename.isEmpty() && refSeed.isEmpty()) {
        return false;
    }
    QString candFilename = resourceFilename(candidate);
    QString candSeed = resourceSeedText(candidate);
    if (!refFilename.isEmpty() && !candFilename.isEmpty()
        && refFilename == candFilename && refSeed == candSeed) {
        return true;
    }
    return !refSeed.isEmpty() && refSeed == candSeed;
}

void collectResourcePaths(const QJsonValue &value, const QVariantList &prefix, QList<QVariantList> &out)
{
    if (value.isObject()) {
        QJsonObject obj = value.toObject();
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            QString mapped = JsonMapper::isLoaded() ? JsonMapper::mapKey(it.key()) : QString();
            const QString &key = it.key();
            bool isResourceKey = (key == kKeyShipResourceLong || key == kKeyShipResource
                                  || key == kKeyCurrentShipLong || key == kKeyCurrentShip);
            if (!isResourceKey && !mapped.isEmpty()) {
                isResourceKey = (mapped == kKeyShipResourceLong || mapped == kKeyCurrentShipLong);
            }
            if (isResourceKey && it.value().isObject()) {
                QVariantList path = prefix;
                path << key;
                out.append(path);
            }
            QVariantList nextPrefix = prefix;
            nextPrefix << key;
            collectResourcePaths(it.value(), nextPrefix, out);
        }
    } else if (value.isArray()) {
        QJsonArray array = value.toArray();
        for (int i = 0; i < array.size(); ++i) {
            QVariantList nextPrefix = prefix;
            nextPrefix << i;
            collectResourcePaths(array.at(i), nextPrefix, out);
        }
    }
}

bool isEmptyShipSlot(const QJsonObject &ship)
{
    QString name = ship.value(kKeyShipNameLong).toString();
    if (name.isEmpty()) {
        name = ship.value(kKeyShipName).toString();
    }

    QJsonObject resource = ship.value(kKeyResourceLong).toObject();
    if (resource.isEmpty()) {
        resource = ship.value(kKeyResource).toObject();
    }

    QString filename = resource.value(kKeyFilenameLong).toString();
    if (filename.isEmpty()) {
        filename = resource.value(kKeyFilename).toString();
    }

    QJsonValue seedValue = resource.value(kKeySeedLong);
    if (seedValue.isUndefined()) {
        seedValue = resource.value(kKeySeed);
    }
    QString seedText = seedTextFromValue(seedValue).trimmed();
    bool hasSeed = !seedText.isEmpty() && seedText != "0x0" && seedText != "0x";

    return name.trimmed().isEmpty() && filename.trimmed().isEmpty() && !hasSeed;
}

void setSeedValue(QJsonObject &resource, const QString &raw)
{
    bool ok = false;
    qulonglong seed = raw.startsWith("0x", Qt::CaseInsensitive)
                          ? raw.mid(2).toULongLong(&ok, 16)
                          : raw.toULongLong(&ok, 10);
    if (!ok) {
        return;
    }
    QString formatted = formattedSeedHex(seed);
    if (resource.contains(kKeySeedLong)) {
        QJsonValue seedValue = resource.value(kKeySeedLong);
        if (seedValue.isArray()) {
            QJsonArray array = seedValue.toArray();
            if (array.size() < 2) {
                array = QJsonArray{true, formatted};
            } else {
                array[0] = true;
                array[1] = formatted;
            }
            resource.insert(kKeySeedLong, array);
        } else {
            resource.insert(kKeySeedLong, formatted);
        }
        return;
    }
    if (resource.contains(kKeySeed)) {
        QJsonValue seedValue = resource.value(kKeySeed);
        if (seedValue.isArray()) {
            QJsonArray array = seedValue.toArray();
            if (array.size() < 2) {
                array = QJsonArray{true, formatted};
            } else {
                array[0] = true;
                array[1] = formatted;
            }
            resource.insert(kKeySeed, array);
        } else {
            resource.insert(kKeySeed, formatted);
        }
        return;
    }
    resource.insert(kKeySeedLong, QJsonArray{true, formatted});
}

QJsonValue remapKeysToLong(const QJsonValue &value)
{
    if (value.isObject()) {
        QJsonObject out;
        QSet<QString> longKeys;
        QJsonObject obj = value.toObject();
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            QString mapped = JsonMapper::mapKey(it.key());
            QJsonValue remappedValue = remapKeysToLong(it.value());
            bool isLong = (it.key() == mapped);
            if (out.contains(mapped) && !isLong && longKeys.contains(mapped)) {
                continue;
            }
            if (isLong) {
                longKeys.insert(mapped);
            }
            out.insert(mapped, remappedValue);
        }
        return out;
    }
    if (value.isArray()) {
        QJsonArray array = value.toArray();
        QJsonArray out;
        for (const QJsonValue &element : array) {
            out.append(remapKeysToLong(element));
        }
        return out;
    }
    return value;
}

QJsonValue remapKeysToShort(const QJsonValue &value, const QHash<QString, QString> &longToShort)
{
    if (value.isObject()) {
        QJsonObject out;
        QJsonObject obj = value.toObject();
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            QString mapped = longToShort.value(it.key(), it.key());
            bool isLong = (mapped != it.key());
            if (out.contains(mapped) && isLong) {
                continue;
            }
            out.insert(mapped, remapKeysToShort(it.value(), longToShort));
        }
        return out;
    }
    if (value.isArray()) {
        QJsonArray array = value.toArray();
        QJsonArray out;
        for (const QJsonValue &element : array) {
            out.append(remapKeysToShort(element, longToShort));
        }
        return out;
    }
    return value;
}
}

ShipManagerPage::ShipManagerPage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

bool ShipManagerPage::loadFromFile(const QString &filePath, QString *errorMessage)
{
    QByteArray contentBytes;
    QJsonDocument doc;
    std::shared_ptr<LosslessJsonDocument> lossless;
    if (!SaveCache::loadWithLossless(filePath, &contentBytes, &doc, &lossless, errorMessage)) {
        return false;
    }
    return loadFromPrepared(filePath, doc, lossless, errorMessage);
}

bool ShipManagerPage::loadFromPrepared(
    const QString &filePath, const QJsonDocument &doc,
    const std::shared_ptr<LosslessJsonDocument> &losslessDoc, QString *errorMessage)
{
    if (!losslessDoc) {
        if (errorMessage) {
            *errorMessage = tr("Failed to load lossless JSON.");
        }
        return false;
    }

    rootDoc_ = doc;
    losslessDoc_ = losslessDoc;
    currentFilePath_ = filePath;
    hasUnsavedChanges_ = false;
    if (!syncRootFromLossless(errorMessage)) {
        return false;
    }
    updateActiveContext();
    rebuildShipList();
    return true;
}

bool ShipManagerPage::saveChanges(QString *errorMessage)
{
    if (currentFilePath_.isEmpty() || rootDoc_.isNull())
    {
        if (errorMessage)
        {
            *errorMessage = tr("No save loaded.");
        }
        return false;
    }

    if (currentFilePath_.endsWith(".hg", Qt::CaseInsensitive))
    {
        if (losslessDoc_) {
            if (!SaveEncoder::encodeSave(currentFilePath_, losslessDoc_->toJson(false), errorMessage)) {
                return false;
            }
            hasUnsavedChanges_ = false;
            return true;
        }
        if (!SaveEncoder::encodeSave(currentFilePath_, rootDoc_.object(), errorMessage)) {
            return false;
        }
        hasUnsavedChanges_ = false;
        return true;
    }

    QFile file(currentFilePath_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if (errorMessage)
        {
            *errorMessage = tr("Unable to write %1").arg(currentFilePath_);
        }
        return false;
    }
    if (losslessDoc_) {
        file.write(losslessDoc_->toJson(true));
    } else {
        file.write(rootDoc_.toJson(QJsonDocument::Indented));
    }
    hasUnsavedChanges_ = false;
    return true;
}

bool ShipManagerPage::hasLoadedSave() const
{
    return !rootDoc_.isNull();
}

bool ShipManagerPage::hasUnsavedChanges() const
{
    return hasUnsavedChanges_;
}

const QString &ShipManagerPage::currentFilePath() const
{
    return currentFilePath_;
}

void ShipManagerPage::clearLoadedSave()
{
    currentFilePath_.clear();
    rootDoc_ = QJsonDocument();
    losslessDoc_.reset();
    hasUnsavedChanges_ = false;
    usingExpeditionContext_ = false;
    ships_.clear();
    activeShipIndex_ = -1;
    if (shipCombo_) {
        shipCombo_->blockSignals(true);
        shipCombo_->clear();
        shipCombo_->blockSignals(false);
    }
    setActiveShip(-1);
}

void ShipManagerPage::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);

    auto *row = new QHBoxLayout();
    row->setSpacing(8);
    row->addWidget(new QLabel(tr("Ship:"), this));
    shipCombo_ = new QComboBox(this);
    shipCombo_->setFixedWidth(280);
    row->addWidget(shipCombo_);
    importButton_ = new QPushButton(tr("Import"), this);
    exportButton_ = new QPushButton(tr("Export"), this);
    row->addWidget(importButton_);
    row->addWidget(exportButton_);
    row->addStretch();
    layout->addLayout(row);

    scrollArea_ = new QScrollArea(this);
    scrollArea_->setWidgetResizable(true);
    layout->addWidget(scrollArea_);

    formWidget_ = new QWidget(this);
    auto *mainLayout = new QVBoxLayout(formWidget_);
    mainLayout->setContentsMargins(16, 16, 16, 16);

    auto *container = new QWidget(formWidget_);
    container->setFixedWidth(500);
    auto *contentLayout = new QVBoxLayout(container);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(12);

    auto *form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);

    nameField_ = new QLineEdit(container);
    form->addRow(tr("Name:"), nameField_);

    typeCombo_ = new QComboBox(container);
    typeCombo_->addItems(QStringList()
                         << QStringLiteral("Fighter")
                         << QStringLiteral("Shuttle")
                         << QStringLiteral("Hauler")
                         << QStringLiteral("Explorer")
                         << QStringLiteral("Exotic")
                         << QStringLiteral("Solar")
                         << QStringLiteral("Interceptor")
                         << QStringLiteral("Living"));
    form->addRow(tr("Type:"), typeCombo_);

    classCombo_ = new QComboBox(container);
    classCombo_->addItems(QStringList()
                          << QStringLiteral("C")
                          << QStringLiteral("B")
                          << QStringLiteral("A")
                          << QStringLiteral("S"));
    form->addRow(tr("Class:"), classCombo_);

    seedField_ = new QLineEdit(container);
    auto *seedRow = new QHBoxLayout();
    auto *seedWrap = new QWidget(container);
    auto *seedButton = new QPushButton(tr("Generate"), container);
    seedButton->setMinimumWidth(90);
    seedRow->addWidget(seedField_);
    seedRow->addWidget(seedButton);
    seedRow->setContentsMargins(0, 0, 0, 0);
    seedWrap->setLayout(seedRow);
    form->addRow(tr("Seed:"), seedWrap);

    useOldColours_ = new QCheckBox(tr("Use Old Colours"), container);
    form->addRow(QString(), useOldColours_);

    contentLayout->addLayout(form);

    auto *baseStats = new QGroupBox(tr("Base Stats"), container);
    auto *statsLayout = new QFormLayout(baseStats);
    statsLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    statsLayout->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);

    healthField_ = new QLineEdit(baseStats);
    healthField_->setReadOnly(true);
    statsLayout->addRow(tr("Health:"), healthField_);

    shieldField_ = new QLineEdit(baseStats);
    shieldField_->setReadOnly(true);
    statsLayout->addRow(tr("Shield:"), shieldField_);

    damageField_ = new QLineEdit(baseStats);
    damageField_->setReadOnly(true);
    statsLayout->addRow(tr("Damage:"), damageField_);

    shieldsField_ = new QLineEdit(baseStats);
    shieldsField_->setReadOnly(true);
    statsLayout->addRow(tr("Shields:"), shieldsField_);

    hyperdriveField_ = new QLineEdit(baseStats);
    hyperdriveField_->setReadOnly(true);
    statsLayout->addRow(tr("Hyperdrive:"), hyperdriveField_);

    maneuverField_ = new QLineEdit(baseStats);
    maneuverField_->setReadOnly(true);
    statsLayout->addRow(tr("Maneuverability:"), maneuverField_);

    contentLayout->addWidget(baseStats);
    contentLayout->addStretch();

    mainLayout->addWidget(container);
    mainLayout->addStretch();
    scrollArea_->setWidget(formWidget_);

    connect(shipCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) {
                if (index < 0 || index >= ships_.size()) {
                    setActiveShip(-1);
                    return;
                }
                setActiveShip(ships_.at(index).index);
            });
    connect(importButton_, &QPushButton::clicked, this, &ShipManagerPage::importShip);
    connect(exportButton_, &QPushButton::clicked, this, &ShipManagerPage::exportShip);

    connect(nameField_, &QLineEdit::editingFinished, this, [this]() {
        int index = activeShipIndex_;
        QString value = nameField_->text();
        updateShipAtIndex(index, [value](QJsonObject &ship) {
            if (ship.contains(kKeyShipNameLong)) {
                ship.insert(kKeyShipNameLong, value);
            } else {
                ship.insert(kKeyShipName, value);
            }
        });
    });

    connect(typeCombo_, &QComboBox::currentTextChanged, this, [this](const QString &value) {
        int index = activeShipIndex_;
        QString filename = filenameForType(value);
        if (filename.isEmpty()) {
            return;
        }
        updateShipAtIndex(index, [filename](QJsonObject &ship) {
            QJsonObject resource = ship.value(kKeyResourceLong).toObject();
            if (resource.isEmpty()) {
                resource = ship.value(kKeyResource).toObject();
            }
            if (resource.contains(kKeyFilenameLong)) {
                resource.insert(kKeyFilenameLong, filename);
            } else {
                resource.insert(kKeyFilename, filename);
            }
            ship.insert(resource.contains(kKeyFilenameLong) ? kKeyResourceLong : kKeyResource, resource);
        });
    });

    connect(classCombo_, &QComboBox::currentTextChanged, this, [this](const QString &value) {
        int index = activeShipIndex_;
        updateShipAtIndex(index, [this, value](QJsonObject &ship) {
            updateShipInventoryClass(ship, value);
        });
    });

    connect(seedField_, &QLineEdit::editingFinished, this, [this]() {
        int index = activeShipIndex_;
        QString raw = seedField_->text().trimmed();
        if (raw.isEmpty()) {
            return;
        }
        updateShipAtIndex(index, [raw](QJsonObject &ship) {
            QJsonObject resource = ship.value(kKeyResourceLong).toObject();
            if (resource.isEmpty()) {
                resource = ship.value(kKeyResource).toObject();
            }
            setSeedValue(resource, raw);
            if (ship.contains(kKeyResourceLong)) {
                ship.insert(kKeyResourceLong, resource);
            } else {
                ship.insert(kKeyResource, resource);
            }
        });
    });

    connect(seedButton, &QPushButton::clicked, this, [this]() {
        qulonglong seed = QRandomGenerator::global()->generate64();
        seedField_->setText(formattedSeed(seed));
        seedField_->editingFinished();
    });

    connect(useOldColours_, &QCheckBox::toggled, this, [this](bool checked) {
        int index = activeShipIndex_;
        updateShipAtIndex(index, [checked](QJsonObject &ship) {
            QJsonObject resource = ship.value(kKeyResourceLong).toObject();
            if (resource.isEmpty()) {
                resource = ship.value(kKeyResource).toObject();
            }
            const char *key = resource.contains(kKeyUseLegacyColoursLong)
                                  ? kKeyUseLegacyColoursLong
                                  : (resource.contains(kKeyUsesLegacyColoursLong)
                                         ? kKeyUsesLegacyColoursLong
                                         : kKeyUseLegacyColoursLong);
            resource.insert(key, checked);
            if (ship.contains(kKeyResourceLong)) {
                ship.insert(kKeyResourceLong, resource);
            } else {
                ship.insert(kKeyResource, resource);
            }
        });
    });
}

void ShipManagerPage::updateActiveContext()
{
    usingExpeditionContext_ = false;
    if (!rootDoc_.isObject())
    {
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
    QJsonObject expedition = findTopLevelMappedObject(root, QStringLiteral("ExpeditionContext"));
    if (expedition.contains("6f=") || expedition.contains(kKeyPlayerStateLong)) {
        usingExpeditionContext_ = true;
    }
}

void ShipManagerPage::rebuildShipList()
{
    shipCombo_->blockSignals(true);
    shipCombo_->clear();
    ships_.clear();

    QJsonArray ships = shipOwnershipArray();
    for (int i = 0; i < ships.size(); ++i) {
        QJsonObject ship = ships.at(i).toObject();
        if (isEmptyShipSlot(ship)) {
            continue;
        }
        ShipEntry entry;
        entry.index = i;
        entry.name = shipNameFromObject(ship);
        if (entry.name.isEmpty()) {
            entry.name = tr("Ship %1").arg(i + 1);
        }
        ships_.append(entry);
    }

    for (const ShipEntry &entry : ships_) {
        QString label = tr("[%1] %2").arg(entry.index).arg(entry.name);
        shipCombo_->addItem(label);
    }

    shipCombo_->blockSignals(false);
    if (!ships_.isEmpty()) {
        shipCombo_->setCurrentIndex(0);
        setActiveShip(ships_.at(0).index);
    } else {
        setActiveShip(-1);
    }
}

void ShipManagerPage::setActiveShip(int index)
{
    activeShipIndex_ = index;
    if (index < 0) {
        nameField_->clear();
        seedField_->clear();
        healthField_->clear();
        shieldField_->clear();
        damageField_->clear();
        shieldsField_->clear();
        hyperdriveField_->clear();
        maneuverField_->clear();
        return;
    }

    QJsonArray ships = shipOwnershipArray();
    if (index >= ships.size()) {
        return;
    }
    QJsonObject ship = ships.at(index).toObject();
    refreshShipFields(ship);
}

void ShipManagerPage::importShip()
{
    QString path = QFileDialog::getOpenFileName(
        this,
        tr("Import Ship"),
        QString(),
        tr("Ship Files (*.sh0 *.json *.shp);;JSON Files (*.json);;Companion Ship Files (*.shp);;All Files (*.*)"));
    if (path.isEmpty()) {
        return;
    }

    QJsonArray ships = shipOwnershipArray();
    int emptySlotIndex = -1;
    for (int i = 0; i < ships.size(); ++i) {
        if (isEmptyShipSlot(ships.at(i).toObject())) {
            emptySlotIndex = i;
            break;
        }
    }

    bool importedIntoEmptySlot = false;
    int targetIndex = -1;
    if (emptySlotIndex >= 0) {
        targetIndex = emptySlotIndex;
        importedIntoEmptySlot = true;
    } else {
        if (activeShipIndex_ < 0) {
            QMessageBox::information(this, tr("Import Ship"), tr("Select a ship to replace."));
            return;
        }
        if (activeShipIndex_ >= ships.size()) {
            QMessageBox::warning(this, tr("Import Ship"), tr("Selected ship is unavailable."));
            return;
        }
        QJsonObject selectedShip = ships.at(activeShipIndex_).toObject();
        QString selectedName = shipNameFromObject(selectedShip).trimmed();
        if (selectedName.isEmpty()) {
            selectedName = tr("Ship %1").arg(activeShipIndex_ + 1);
        }
        if (QMessageBox::question(this,
                                  tr("Replace Ship"),
                                  tr("Replace %1 with the imported data?").arg(selectedName))
            != QMessageBox::Yes) {
            return;
        }
        targetIndex = activeShipIndex_;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Import Ship"), tr("Unable to open %1").arg(path));
        return;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        QMessageBox::warning(this, tr("Import Ship"), tr("Invalid ship file."));
        return;
    }

    QJsonObject root = doc.object();
    bool isCompanionExport = root.contains(QStringLiteral("Ship"))
        && root.contains(QStringLiteral("FileVersion"))
        && root.contains(QStringLiteral("Thumbnail"));
    if (isCompanionExport) {
        if (QMessageBox::question(this,
                                  tr("Import Ship"),
                                  tr("This ship appears to be exported by No Man's Sky Companion. Import it into NMSSaveEditor?"))
            != QMessageBox::Yes) {
            return;
        }
    }

    ensureMappingLoaded();
    QHash<QString, QString> longToShort;
    const auto mapping = JsonMapper::mapping();
    for (auto it = mapping.begin(); it != mapping.end(); ++it) {
        longToShort.insert(it.value(), it.key());
    }
    QJsonObject sourceObject = root;
    if (root.contains(QStringLiteral("Ship")) && root.value(QStringLiteral("Ship")).isObject()) {
        sourceObject = root.value(QStringLiteral("Ship")).toObject();
    }
    QJsonObject shipData = remapKeysToShort(sourceObject, longToShort).toObject();
    if (shipData.isEmpty()) {
        QMessageBox::warning(this, tr("Import Ship"), tr("No ship data found."));
        return;
    }

    updateShipAtIndex(targetIndex, [shipData](QJsonObject &ship) {
        ship = shipData;
    });
    if (importedIntoEmptySlot) {
        rebuildShipList();
        for (int i = 0; i < ships_.size(); ++i) {
            if (ships_.at(i).index == targetIndex) {
                shipCombo_->setCurrentIndex(i);
                setActiveShip(targetIndex);
                break;
            }
        }
    }
    emit statusMessage(tr("Imported ship from %1").arg(QFileInfo(path).fileName()));
}

void ShipManagerPage::exportShip()
{
    if (activeShipIndex_ < 0) {
        QMessageBox::information(this, tr("Export Ship"), tr("Select a ship first."));
        return;
    }

    QJsonArray ships = shipOwnershipArray();
    if (activeShipIndex_ >= ships.size()) {
        QMessageBox::warning(this, tr("Export Ship"), tr("Selected ship is unavailable."));
        return;
    }

    QJsonObject ship = ships.at(activeShipIndex_).toObject();
    ensureMappingLoaded();
    QJsonObject exportShip = remapKeysToLong(ship).toObject();

    QString name = shipNameFromObject(ship).trimmed();
    if (name.isEmpty()) {
        name = shipTypeFromObject(ship).trimmed();
    }
    if (name.isEmpty()) {
        name = tr("Ship%1").arg(activeShipIndex_ + 1);
    }
    QString fileName = name + ".sh0";
    QString path = QFileDialog::getSaveFileName(
        this,
        tr("Export Ship"),
        fileName,
        tr("Ship Files (*.sh0 *.json);;JSON Files (*.json)"));
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Export Ship"), tr("Unable to write %1").arg(path));
        return;
    }
    QJsonDocument doc(exportShip);
    file.write(doc.toJson(QJsonDocument::Indented));
    emit statusMessage(tr("Exported ship to %1").arg(QFileInfo(path).fileName()));
}

QJsonObject ShipManagerPage::activePlayerState() const
{
    QJsonObject root = rootDoc_.object();
    if (usingExpeditionContext_) {
        QJsonObject expedition = root.value(kKeyExpeditionContext).toObject();
        if (expedition.isEmpty()) {
            expedition = root.value(kKeyExpeditionContextLong).toObject();
        }
        QJsonObject player = expedition.value("6f=").toObject();
        if (player.isEmpty()) {
            player = expedition.value(kKeyPlayerStateLong).toObject();
        }
        return player;
    }

    QJsonObject base = root.value(kKeyPlayerState).toObject();
    if (base.isEmpty()) {
        base = root.value(kKeyBaseContextLong).toObject();
    }
    QJsonObject player = base.value("6f=").toObject();
    if (player.isEmpty()) {
        player = base.value(kKeyPlayerStateLong).toObject();
    }
    return player;
}

QVariantList ShipManagerPage::shipOwnershipPath() const
{
    return shipOwnershipPathForContext(usingExpeditionContext_);
}

QVariantList ShipManagerPage::shipOwnershipPathForContext(bool expedition) const
{
    if (expedition) {
        QVariantList shortPath = {kKeyExpeditionContext, "6f=", kKeyShipOwnership};
        if (valueAtPath(rootDoc_.object(), shortPath).isArray()) {
            return shortPath;
        }
        QVariantList mixedPath = {kKeyExpeditionContext, "6f=", kKeyShipOwnershipLong};
        if (valueAtPath(rootDoc_.object(), mixedPath).isArray()) {
            return mixedPath;
        }
        QVariantList longPath = {kKeyExpeditionContextLong, kKeyPlayerStateLong, kKeyShipOwnershipLong};
        return longPath;
    }

    QVariantList shortPath = {kKeyPlayerState, "6f=", kKeyShipOwnership};
    if (valueAtPath(rootDoc_.object(), shortPath).isArray()) {
        return shortPath;
    }
    QVariantList mixedPath = {kKeyPlayerState, "6f=", kKeyShipOwnershipLong};
    if (valueAtPath(rootDoc_.object(), mixedPath).isArray()) {
        return mixedPath;
    }
    QVariantList longPath = {kKeyBaseContextLong, kKeyPlayerStateLong, kKeyShipOwnershipLong};
    return longPath;
}

QVariantList ShipManagerPage::playerStatePathForContext(bool expedition) const
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

QVariantList ShipManagerPage::contextRootPathForContext(bool expedition) const
{
    if (!rootDoc_.isObject()) {
        return {};
    }
    QJsonObject root = rootDoc_.object();
    if (expedition) {
        if (root.contains(kKeyExpeditionContext)) {
            return {kKeyExpeditionContext};
        }
        if (root.contains(kKeyExpeditionContextLong)) {
            return {kKeyExpeditionContextLong};
        }
        QString mappedKey = findTopLevelMappedKeyName(root, QStringLiteral("ExpeditionContext"));
        if (!mappedKey.isEmpty()) {
            return {mappedKey};
        }
        return {};
    }
    if (root.contains(kKeyBaseContextLong)) {
        return {kKeyBaseContextLong};
    }
    if (root.contains(kKeyPlayerState)) {
        return {kKeyPlayerState};
    }
    QString mappedKey = findTopLevelMappedKeyName(root, QStringLiteral("BaseContext"));
    if (!mappedKey.isEmpty()) {
        return {mappedKey};
    }
    return {};
}

QJsonArray ShipManagerPage::shipOwnershipArray() const
{
    QVariantList path = shipOwnershipPath();
    QJsonValue value = valueAtPath(rootDoc_.object(), path);
    return value.toArray();
}

void ShipManagerPage::updateShipAtIndex(int index, const std::function<void(QJsonObject &)> &mutator)
{
    if (index < 0) {
        return;
    }
    QVariantList path = shipOwnershipPath();
    QJsonValue current = valueAtPath(rootDoc_.object(), path);
    if (!current.isArray()) {
        return;
    }
    QJsonArray ships = current.toArray();
    if (index >= ships.size()) {
        return;
    }
    QJsonObject ship = ships.at(index).toObject();
    QJsonObject original = ship;
    QJsonObject oldResource = resourceObjectFromShip(original);
    mutator(ship);
    if (ship == original) {
        return;
    }
    QJsonObject newResource = resourceObjectFromShip(ship);
    ships.replace(index, ship);
    applyValueAtPath(path, ships);
    emit statusMessage(tr("Pending changes — remember to Save!"));
    QString updatedName = shipNameFromObject(ship);
    for (int i = 0; i < ships_.size(); ++i) {
        if (ships_.at(i).index == index) {
            ships_[i].name = updatedName;
            shipCombo_->setItemText(i, tr("[%1] %2").arg(index).arg(updatedName.isEmpty()
                                                                     ? tr("Ship %1").arg(index + 1)
                                                                     : updatedName));
            break;
        }
    }
    refreshShipFields(ship);

    if (oldResource != newResource && !oldResource.isEmpty() && !newResource.isEmpty()) {
        updatePlayerShipResources(oldResource, newResource);
    }

    if (usingExpeditionContext_) {
        QVariantList basePath = shipOwnershipPathForContext(false);
        if (!basePath.isEmpty() && basePath != path) {
            updateShipAtIndexOnPath(basePath, index, mutator, false);
        }
    }
}

void ShipManagerPage::updateShipAtIndexOnPath(const QVariantList &path, int index,
                                              const std::function<void(QJsonObject &)> &mutator,
                                              bool updateUi)
{
    if (index < 0 || path.isEmpty()) {
        return;
    }
    QJsonValue current = valueAtPath(rootDoc_.object(), path);
    if (!current.isArray()) {
        return;
    }
    QJsonArray ships = current.toArray();
    if (index >= ships.size()) {
        return;
    }
    QJsonObject ship = ships.at(index).toObject();
    QJsonObject original = ship;
    mutator(ship);
    if (ship == original) {
        return;
    }
    ships.replace(index, ship);
    applyValueAtPath(path, ships);

    if (!updateUi) {
        return;
    }
    emit statusMessage(tr("Pending changes — remember to Save!"));
    QString updatedName = shipNameFromObject(ship);
    for (int i = 0; i < ships_.size(); ++i) {
        if (ships_.at(i).index == index) {
            ships_[i].name = updatedName;
            shipCombo_->setItemText(i, tr("[%1] %2").arg(index).arg(updatedName.isEmpty()
                                                                     ? tr("Ship %1").arg(index + 1)
                                                                     : updatedName));
            break;
        }
    }
    refreshShipFields(ship);
}

void ShipManagerPage::updatePlayerShipResources(const QJsonObject &oldResource, const QJsonObject &newResource)
{
    QVariantList activePath = playerStatePathForContext(usingExpeditionContext_);
    if (!activePath.isEmpty()) {
        updatePlayerStateResourceAtPath(activePath, oldResource, newResource);
    }
    if (usingExpeditionContext_) {
        QVariantList basePath = playerStatePathForContext(false);
        if (!basePath.isEmpty() && basePath != activePath) {
            updatePlayerStateResourceAtPath(basePath, oldResource, newResource);
        }
    }
    QVariantList activeContextPath = contextRootPathForContext(usingExpeditionContext_);
    if (!activeContextPath.isEmpty()) {
        updateContextResources(activeContextPath, oldResource, newResource);
    }
    if (usingExpeditionContext_) {
        QVariantList baseContextPath = contextRootPathForContext(false);
        if (!baseContextPath.isEmpty() && baseContextPath != activeContextPath) {
            updateContextResources(baseContextPath, oldResource, newResource);
        }
    }
}

bool ShipManagerPage::updatePlayerStateResourceAtPath(const QVariantList &path,
                                                      const QJsonObject &oldResource,
                                                      const QJsonObject &newResource)
{
    QJsonValue playerValue = valueAtPath(rootDoc_.object(), path);
    if (!playerValue.isObject()) {
        return false;
    }
    QJsonObject player = playerValue.toObject();
    bool updated = false;

    auto updateIfMatching = [&](const char *longKey, const char *shortKey) {
        if (player.contains(longKey) && player.value(longKey).isObject()) {
            QJsonObject existing = player.value(longKey).toObject();
            if (resourceMatches(existing, oldResource)) {
                player.insert(longKey, newResource);
                updated = true;
            }
            return;
        }
        if (player.contains(shortKey) && player.value(shortKey).isObject()) {
            QJsonObject existing = player.value(shortKey).toObject();
            if (resourceMatches(existing, oldResource)) {
                player.insert(shortKey, newResource);
                updated = true;
            }
        }
    };

    updateIfMatching(kKeyShipResourceLong, kKeyShipResource);
    updateIfMatching(kKeyCurrentShipLong, kKeyCurrentShip);

    if (!updated) {
        return false;
    }
    applyValueAtPath(path, player);
    return true;
}

void ShipManagerPage::updateContextResources(const QVariantList &contextPath,
                                             const QJsonObject &oldResource,
                                             const QJsonObject &newResource)
{
    ensureMappingLoaded();
    QJsonValue contextValue = contextPath.isEmpty()
                                  ? QJsonValue(rootDoc_.object())
                                  : valueAtPath(rootDoc_.object(), contextPath);
    if (contextValue.isUndefined() || contextValue.isNull()) {
        return;
    }
    QList<QVariantList> paths;
    collectResourcePaths(contextValue, QVariantList(), paths);
    if (paths.isEmpty()) {
        return;
    }
    for (const QVariantList &relative : paths) {
        QVariantList fullPath = contextPath;
        fullPath.append(relative);
        QJsonValue current = valueAtPath(rootDoc_.object(), fullPath);
        if (!current.isObject()) {
            continue;
        }
        QJsonObject existing = current.toObject();
        if (resourceMatches(existing, oldResource)) {
            applyValueAtPath(fullPath, newResource);
        }
    }
}

void ShipManagerPage::updateShipInventoryClass(QJsonObject &ship, const QString &value)
{
    auto updateInventory = [&value](QJsonObject &inventory) {
        QJsonObject classObj = inventory.value(kKeyInventoryClassLong).toObject();
        if (classObj.isEmpty()) {
            classObj = inventory.value(kKeyInventoryClass).toObject();
        }
        if (classObj.contains(kKeyInventoryClassValueLong)) {
            classObj.insert(kKeyInventoryClassValueLong, value);
        } else {
            classObj.insert(kKeyInventoryClassValue, value);
        }
        if (inventory.contains(kKeyInventoryClassLong)) {
            inventory.insert(kKeyInventoryClassLong, classObj);
        } else {
            inventory.insert(kKeyInventoryClass, classObj);
        }
    };

    auto updateIfPresent = [&ship, &updateInventory](const char *longKey, const char *shortKey) {
        if (ship.contains(longKey)) {
            QJsonObject inventory = ship.value(longKey).toObject();
            updateInventory(inventory);
            ship.insert(longKey, inventory);
            return;
        }
        if (ship.contains(shortKey)) {
            QJsonObject inventory = ship.value(shortKey).toObject();
            updateInventory(inventory);
            ship.insert(shortKey, inventory);
        }
    };

    updateIfPresent(kKeyInventoryLong, kKeyInventory);
    updateIfPresent(kKeyInventoryCargoLong, kKeyInventoryCargo);
    updateIfPresent(kKeyInventoryTechLong, kKeyInventoryTech);
}

QJsonValue ShipManagerPage::valueAtPath(const QJsonValue &root, const QVariantList &path) const
{
    QJsonValue result = root;
    for (const QVariant &segment : path)
    {
        if (result.isObject())
        {
            result = result.toObject().value(segment.toString());
        }
        else if (result.isArray())
        {
            result = result.toArray().at(segment.toInt());
        }
        else
        {
            return QJsonValue();
        }
    }
    return result;
}

QJsonValue ShipManagerPage::setValueAtPath(const QJsonValue &root, const QVariantList &path, int depth,
                                           const QJsonValue &value) const
{
    if (depth >= path.size())
    {
        return value;
    }

    QVariant segment = path.at(depth);
    if (segment.canConvert<int>() && root.isArray())
    {
        QJsonArray array = root.toArray();
        int index = segment.toInt();
        if (index >= 0 && index < array.size())
        {
            array[index] = setValueAtPath(array.at(index), path, depth + 1, value);
        }
        return array;
    }
    if (segment.canConvert<QString>() && root.isObject())
    {
        QJsonObject obj = root.toObject();
        QString key = segment.toString();
        obj.insert(key, setValueAtPath(obj.value(key), path, depth + 1, value));
        return obj;
    }
    return root;
}

void ShipManagerPage::applyValueAtPath(const QVariantList &path, const QJsonValue &value)
{
    QJsonValue rootValue = rootDoc_.isObject() ? QJsonValue(rootDoc_.object())
                                               : QJsonValue(rootDoc_.array());
    if (valueAtPath(rootValue, path) == value) {
        return;
    }
    QVariantList remappedCheck = SaveJsonModel::remapPathToShort(path);
    if (remappedCheck != path && valueAtPath(rootValue, remappedCheck) == value) {
        return;
    }

    SaveJsonModel::setLosslessValue(losslessDoc_, path, value);
    SaveJsonModel::syncRootFromLossless(losslessDoc_, rootDoc_);
    hasUnsavedChanges_ = true;
}

void ShipManagerPage::refreshShipFields(const QJsonObject &ship)
{
    QSignalBlocker blockShipCombo(shipCombo_);
    QSignalBlocker blockName(nameField_);
    QSignalBlocker blockType(typeCombo_);
    QSignalBlocker blockClass(classCombo_);
    QSignalBlocker blockSeed(seedField_);
    QSignalBlocker blockColours(useOldColours_);

    nameField_->setText(shipNameFromObject(ship));

    QString type = shipTypeFromObject(ship);
    if (!type.isEmpty()) {
        int idx = typeCombo_->findText(type);
        if (idx >= 0) {
            typeCombo_->setCurrentIndex(idx);
        }
    } else {
        typeCombo_->setCurrentIndex(-1);
    }

    QString shipClass = shipClassFromObject(ship);
    if (!shipClass.isEmpty()) {
        int idx = classCombo_->findText(shipClass);
        if (idx >= 0) {
            classCombo_->setCurrentIndex(idx);
        }
    } else {
        classCombo_->setCurrentIndex(-1);
    }

    seedField_->setText(shipSeedFromObject(ship));
    useOldColours_->setChecked(shipUseLegacyColours(ship));

    QJsonObject player = activePlayerState();
    QJsonValue healthValue = player.value(kKeyShipHealth);
    if (healthValue.isUndefined()) {
        healthValue = player.value(kKeyShipHealthShort);
    }
    if (healthValue.isUndefined()) {
        healthValue = player.value(kKeyShipHealthLegacyShort);
    }
    if (healthValue.isUndefined()) {
        healthValue = findMappedKey(player, QStringLiteral("ShipHealth"));
    }
    QJsonValue shieldValue = player.value(kKeyShipShield);
    if (shieldValue.isUndefined()) {
        shieldValue = player.value(kKeyShipShieldShort);
    }
    if (shieldValue.isUndefined()) {
        shieldValue = player.value(kKeyShipShieldLegacyShort);
    }
    if (shieldValue.isUndefined()) {
        shieldValue = player.value(kKeyShipShieldLegacy);
    }
    if (shieldValue.isUndefined()) {
        shieldValue = findMappedKey(player, QStringLiteral("ShipShield"));
    }
    if (shieldValue.isUndefined()) {
        shieldValue = findMappedKey(player, QStringLiteral("Shield"));
    }

    if (healthValue.isUndefined()) {
        healthField_->clear();
    } else {
        healthField_->setText(formatNumber(healthValue.toDouble()));
    }
    if (shieldValue.isUndefined()) {
        shieldField_->clear();
    } else {
        shieldField_->setText(formatNumber(shieldValue.toDouble()));
    }

    damageField_->setText(formatNumber(shipStatValue(ship, kStatShipDamage)));
    shieldsField_->setText(formatNumber(shipStatValue(ship, kStatShipShield)));
    hyperdriveField_->setText(formatNumber(shipStatValue(ship, kStatShipHyperdrive)));
    maneuverField_->setText(formatNumber(shipStatValue(ship, kStatShipAgile)));
}

QString ShipManagerPage::shipNameFromObject(const QJsonObject &ship) const
{
    QString name = ship.value(kKeyShipNameLong).toString();
    if (name.isEmpty()) {
        name = ship.value(kKeyShipName).toString();
    }
    return name;
}

QString ShipManagerPage::shipClassFromObject(const QJsonObject &ship) const
{
    QJsonObject inventory = inventoryObjectForShip(ship);
    QJsonObject classObj = inventory.value(kKeyInventoryClassLong).toObject();
    if (classObj.isEmpty()) {
        classObj = inventory.value(kKeyInventoryClass).toObject();
    }
    QString value = classObj.value(kKeyInventoryClassValueLong).toString();
    if (value.isEmpty()) {
        value = classObj.value(kKeyInventoryClassValue).toString();
    }
    return value;
}

QString ShipManagerPage::shipSeedFromObject(const QJsonObject &ship) const
{
    QJsonObject resource = ship.value(kKeyResourceLong).toObject();
    if (resource.isEmpty()) {
        resource = ship.value(kKeyResource).toObject();
    }
    QJsonValue value = resource.value(kKeySeedLong);
    if (value.isUndefined()) {
        value = resource.value(kKeySeed);
    }
    return seedTextFromValue(value);
}

QString ShipManagerPage::shipTypeFromObject(const QJsonObject &ship) const
{
    QJsonObject resource = ship.value(kKeyResourceLong).toObject();
    if (resource.isEmpty()) {
        resource = ship.value(kKeyResource).toObject();
    }
    QString filename = resource.value(kKeyFilenameLong).toString();
    if (filename.isEmpty()) {
        filename = resource.value(kKeyFilename).toString();
    }
    return typeFromFilename(filename);
}

bool ShipManagerPage::shipUseLegacyColours(const QJsonObject &ship) const
{
    QJsonObject resource = ship.value(kKeyResourceLong).toObject();
    if (resource.isEmpty()) {
        resource = ship.value(kKeyResource).toObject();
    }
    if (resource.contains(kKeyUseLegacyColoursLong)) {
        return resource.value(kKeyUseLegacyColoursLong).toBool();
    }
    if (resource.contains(kKeyUsesLegacyColoursLong)) {
        return resource.value(kKeyUsesLegacyColoursLong).toBool();
    }
    if (resource.contains(kKeyUseLegacyColours)) {
        return resource.value(kKeyUseLegacyColours).toBool();
    }
    if (resource.contains(kKeyUsesLegacyColours)) {
        return resource.value(kKeyUsesLegacyColours).toBool();
    }
    return false;
}

double ShipManagerPage::shipStatValue(const QJsonObject &ship, const QString &statId) const
{
    auto statFromInventory = [&statId, this](const QJsonObject &inventory) -> double {
        QJsonValue value = inventory.value(kKeyBaseStatValuesLong);
        if (value.isUndefined()) {
            value = inventory.value(kKeyBaseStatValues);
        }
        if (!value.isArray()) {
            return 0.0;
        }
        QJsonArray stats = value.toArray();
        for (const QJsonValue &statValue : stats) {
            QJsonObject stat = statValue.toObject();
            QString id = stat.value(kKeyBaseStatIdLong).toString();
            if (id.isEmpty()) {
                id = stat.value(kKeyBaseStatId).toString();
            }
            QString compareId = id;
            QString compareStat = statId;
            if (compareId.startsWith('^')) {
                compareId.remove(0, 1);
            }
            if (compareStat.startsWith('^')) {
                compareStat.remove(0, 1);
            }
            if (compareId == compareStat) {
                QJsonValue raw = stat.value(kKeyBaseStatValueLong);
                if (raw.isUndefined()) {
                    raw = stat.value(kKeyBaseStatValue);
                }
                return raw.toDouble();
            }
        }
        return 0.0;
    };

    QJsonObject inventory = inventoryObjectForShip(ship);
    double value = statFromInventory(inventory);
    if (value != 0.0) {
        return value;
    }
    if (ship.contains(kKeyInventoryCargoLong) || ship.contains(kKeyInventoryCargo)) {
        QJsonObject cargo = ship.contains(kKeyInventoryCargoLong)
                                ? ship.value(kKeyInventoryCargoLong).toObject()
                                : ship.value(kKeyInventoryCargo).toObject();
        value = statFromInventory(cargo);
        if (value != 0.0) {
            return value;
        }
    }
    if (ship.contains(kKeyInventoryTechLong) || ship.contains(kKeyInventoryTech)) {
        QJsonObject tech = ship.contains(kKeyInventoryTechLong)
                               ? ship.value(kKeyInventoryTechLong).toObject()
                               : ship.value(kKeyInventoryTech).toObject();
        value = statFromInventory(tech);
    }
    return value;
}

QJsonObject ShipManagerPage::inventoryObjectForShip(const QJsonObject &ship) const
{
    if (ship.contains(kKeyInventoryLong)) {
        return ship.value(kKeyInventoryLong).toObject();
    }
    if (ship.contains(kKeyInventory)) {
        return ship.value(kKeyInventory).toObject();
    }
    return QJsonObject();
}

QString ShipManagerPage::formatNumber(double value) const
{
    return QString::number(value, 'f', (value == static_cast<int>(value)) ? 0 : 6)
        .remove(QRegularExpression("0+$"))
        .remove(QRegularExpression("\\.$"));
}

QString ShipManagerPage::formattedSeed(qulonglong seed) const
{
    return formattedSeedHex(seed);
}

bool ShipManagerPage::syncRootFromLossless(QString *errorMessage)
{
    return SaveJsonModel::syncRootFromLossless(losslessDoc_, rootDoc_, errorMessage);
}
