#include "inventory/InventoryEditorPage.h"

#include "core/SaveDecoder.h"
#include "core/SaveEncoder.h"
#include "core/ResourceLocator.h"
#include "inventory/InventoryGridWidget.h"
#include "registry/IconRegistry.h"
#include "registry/ItemDefinitionRegistry.h"

#include <cmath>
#include <QFile>
#include <QFileInfo>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QIntValidator>
#include <QPushButton>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QScrollArea>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>

namespace
{
    const char *kKeyActiveContext = "XTp";
    const char *kKeyExpeditionContext = "2YS";

static bool isExplicitlyEmptySeed(const QJsonValue &v)
{
    if (v.isNull() || v.isUndefined()) return false;
    if (v.isArray())
    {
        QJsonArray arr = v.toArray();
        return arr.size() >= 2 && !arr.at(0).toBool() && arr.at(1).toString() == "0x0";
    }
    QString s = v.toString();
    return s == "0x0" || s == "0x";
}

static bool hasInventorySlots(const QJsonObject &inventory)
{
    QJsonArray slotList = inventory.value(":No").toArray();
    if (!slotList.isEmpty()) return true;
    QJsonArray valid = inventory.value("hl?").toArray();
    if (!valid.isEmpty()) return true;
    return false;
}

static QJsonObject multitoolStoreObject(const QJsonObject &mtData)
{
    if (mtData.contains("OsQ") && mtData.value("OsQ").isObject()) {
        return mtData.value("OsQ").toObject();
    }
    return mtData;
}

static QJsonObject multitoolDataObject(const QJsonObject &item)
{
    QJsonValue mt = item.value("97S");
    if (mt.isObject()) return mt.toObject();
    return item;
}

static QVariantList findMultitoolPath(const QJsonObject &root, const QVariantList &base) {
    // Try SuJ directly under base (Common for modern saves)
    QVariantList p1 = base; p1 << "SuJ";
    QJsonValue v1 = InventoryEditorPage::valueAtPath(root, p1);
    if (v1.isArray() && !v1.toArray().isEmpty()) return p1;

    // Try 97S -> SuJ (Alternative structure)
    QVariantList p2 = base; p2 << "97S" << "SuJ";
    QJsonValue v2 = InventoryEditorPage::valueAtPath(root, p2);
    if (v2.isArray() && !v2.toArray().isEmpty()) return p2;

    // Default to Kgt (Active weapon only)
    QVariantList p3 = base; p3 << "Kgt";
    return p3;
}
    const char *kKeyPlayerState = "vLc";
    const char *kContextMain = "Main";
    const char *kKeyCommonState = "<h0";
    const char *kKeySeasonData = "Rol";
    const char *kKeySeasonStages = "3Mw";
    const char *kKeyStageMilestones = "kr6";
    const char *kKeyMissionName = "p0c";
    const char *kKeyMissionAmount = "1o9";
    const char *kKeyIcon = "DhC";
    const char *kKeyIconFilename = "93M";
    const char *kKeySeasonState = "qYy";
    const char *kKeyMilestoneValues = "psf";
    const char *kKeyUnits = "wGS";
    const char *kKeyNanites = "7QL";
    const char *kKeyQuicksilver = "kN;";
    const char *kIconUnits = "UNITS";
    const char *kIconNanites = "TECHFRAG";
    const char *kIconQuicksilver = "QUICKSILVER";
    const char *kKeySettlementLocalData = "NEK";
    const char *kKeySettlementStates = "GQA";
    const char *kKeySettlementStats = "@bB";
    const char *kKeySettlementStatId = "QL1";
    const char *kKeySettlementValue = ">MX";
    const char *kKeySettlementPopulation = "x3<";
    const char *kKeySettlementName = "NKm";
}

InventoryEditorPage::InventoryEditorPage(QWidget *parent, InventorySections sections)
    : QWidget(parent)
    , sections_(sections)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    tabs_ = new QTabWidget(this);
    layout->addWidget(tabs_);
}

bool InventoryEditorPage::loadFromFile(const QString &filePath, QString *errorMessage)
{
    QByteArray contentBytes;
    if (filePath.endsWith(".hg", Qt::CaseInsensitive))
    {
        contentBytes = SaveDecoder::decodeSaveBytes(filePath, errorMessage);
    }
    else
    {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly))
        {
            if (errorMessage)
            {
                *errorMessage = tr("Unable to open %1").arg(filePath);
            }
            return false;
        }
        contentBytes = file.readAll();
    }

    if (contentBytes.isEmpty())
    {
        if (errorMessage && errorMessage->isEmpty())
        {
            *errorMessage = tr("No data loaded from %1").arg(filePath);
        }
        return false;
    }

    auto lossless = std::make_shared<LosslessJsonDocument>();
    if (!lossless->parse(contentBytes, errorMessage)) {
        return false;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(contentBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError)
    {
        if (errorMessage)
        {
            *errorMessage = tr("JSON parse error: %1").arg(parseError.errorString());
        }
        return false;
    }

    rootDoc_ = doc;
    losslessDoc_ = lossless;
    currentFilePath_ = filePath;
    hasUnsavedChanges_ = false;
    updateActiveContext();

    QJsonObject player = valueAtPath(rootDoc_.object(), playerBasePath()).toObject();
    selectedShipIndex_ = player.value("aBE").toInt(0);
    
    selectedMultitoolIndex_ = player.value("j3E").toInt(0);

    rebuildTabs();

    emit statusMessage(tr("Loaded %1").arg(QFileInfo(filePath).fileName()));
    return true;
}

bool InventoryEditorPage::hasLoadedSave() const
{
    return !currentFilePath_.isEmpty() && !rootDoc_.isNull();
}

bool InventoryEditorPage::hasUnsavedChanges() const
{
    return hasUnsavedChanges_;
}

const QString &InventoryEditorPage::currentFilePath() const
{
    return currentFilePath_;
}

bool InventoryEditorPage::saveChanges(QString *errorMessage)
{
    if (!hasLoadedSave())
    {
        if (errorMessage)
        {
            *errorMessage = tr("No save loaded.");
        }
        return false;
    }
    if (currentFilePath_.endsWith(".json", Qt::CaseInsensitive))
    {
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
            QJsonObject rootObject = rootDoc_.isObject() ? rootDoc_.object() : QJsonObject();
            QJsonDocument doc(rootObject);
            file.write(doc.toJson(QJsonDocument::Indented));
        }
        hasUnsavedChanges_ = false;
        return true;
    }
    if (losslessDoc_) {
        if (!SaveEncoder::encodeSave(currentFilePath_, losslessDoc_->toJson(false), errorMessage)) {
            return false;
        }
        hasUnsavedChanges_ = false;
        return true;
    }
    QJsonObject rootObject = rootDoc_.isObject() ? rootDoc_.object() : QJsonObject();
    if (!SaveEncoder::encodeSave(currentFilePath_, rootObject, errorMessage)) {
        return false;
    }
    hasUnsavedChanges_ = false;
    return true;
}

void InventoryEditorPage::rebuildTabs()
{
    int currentIndex = tabs_->currentIndex();
    tabs_->clear();

    QList<InventoryDescriptor> descriptors;
    if (sections_.testFlag(InventorySection::Inventories))
    {
        InventoryDescriptor descriptor;
        if (resolveExosuit(descriptor))
        {
            descriptors.append(descriptor);
        }
        if (resolveExosuitTech(descriptor))
        {
            descriptors.append(descriptor);
        }
        if (resolveShip(descriptor))
        {
            descriptors.append(descriptor);
        }
        if (resolveShipTech(descriptor))
        {
            descriptors.append(descriptor);
        }
        if (resolveMultitool(descriptor))
        {
            descriptors.append(descriptor);
        }
        if (resolveFreighter(descriptor))
        {
            descriptors.append(descriptor);
        }
        if (resolveCorvetteCache(descriptor))
        {
            descriptors.append(descriptor);
        }
    }

    QJsonValue rootValue = rootDoc_.isObject() ? QJsonValue(rootDoc_.object())
                                               : QJsonValue(rootDoc_.array());

    for (const InventoryDescriptor &desc : descriptors)
    {
        QJsonArray slotsArray = valueAtPath(rootValue, desc.slotsPath).toArray();
        QJsonArray valid = valueAtPath(rootValue, desc.validPath).toArray();
        QJsonArray specialSlots = !desc.specialSlotsPath.isEmpty() ? valueAtPath(rootValue, desc.specialSlotsPath).toArray() : QJsonArray();

        auto *grid = new InventoryGridWidget(this);
        grid->setInventory(desc.name, slotsArray, valid, specialSlots);
        grid->setCommitHandler([this, desc](const QJsonArray &updatedSlots, const QJsonArray &updatedSpecial)
                               {
            QJsonValue rootValue = rootDoc_.isObject() ? QJsonValue(rootDoc_.object())
                                                      : QJsonValue(rootDoc_.array());
            QJsonValue currentSlots = valueAtPath(rootValue, desc.slotsPath);
            applyDiffAtPath(desc.slotsPath, currentSlots, updatedSlots);
            if (!desc.specialSlotsPath.isEmpty()) {
                QJsonValue currentSpecial = valueAtPath(rootValue, desc.specialSlotsPath);
                applyDiffAtPath(desc.specialSlotsPath, currentSpecial, updatedSpecial);
            } });
        connect(grid, &InventoryGridWidget::statusMessage, this, &InventoryEditorPage::statusMessage);

        auto *scroll = new QScrollArea(this);
        scroll->setWidgetResizable(true);
        scroll->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
        scroll->setWidget(grid);
        
        if (desc.type == InventoryType::Ship || desc.type == InventoryType::Multitool) {
            auto *container = new QWidget(this);
            auto *vbox = new QVBoxLayout(container);
            vbox->setContentsMargins(10, 10, 10, 10);
            
            auto *hbox = new QHBoxLayout();
            auto *label = new QLabel(desc.type == InventoryType::Ship ? tr("Select Ship:") : tr("Select Multitool:"), container);
            label->setStyleSheet("font-weight: bold; color: #00aaff;");
            hbox->addWidget(label);
            
            auto *combo = new QComboBox(container);
            combo->setMinimumWidth(300);
            combo->setStyleSheet("QComboBox { background-color: #2b2b2b; color: white; border: 1px solid #444; padding: 5px; border-radius: 4px; }"
                               "QComboBox:hover { border-color: #00aaff; }"
                               "QComboBox::drop-down { border: none; }"
                               "QComboBox QAbstractItemView { background-color: #2b2b2b; color: white; selection-background-color: #00aaff; }");
            
            QVariantList listPath = playerBasePath();
            QJsonArray list;
            if (desc.type == InventoryType::Ship) {
                listPath << "@Cs";
                list = valueAtPath(rootDoc_.object(), listPath).toArray();
            } else {
                QVariantList mPath = findMultitoolPath(rootDoc_.object(), playerBasePath());
                QJsonValue mVal = valueAtPath(rootDoc_.object(), mPath);
                if (mVal.isArray()) {
                    list = mVal.toArray();
                } else if (mVal.isObject()) {
                    list.append(mVal);
                }
            }
            
            int actualIndex = -1;
            int currentSelection = (desc.type == InventoryType::Ship ? selectedShipIndex_ : selectedMultitoolIndex_);

            for (int i = 0; i < list.size(); ++i) {
                QJsonObject item = list.at(i).toObject();

                if (desc.type == InventoryType::Ship) {
                    QJsonValue s1 = item.value("3R<"); // ShipSeed
                    QJsonValue s2 = item.value("@EL"); // Seed (legacy)
                    QJsonObject resource = item.value("NTx").toObject();
                    QJsonValue s3 = resource.value("@EL"); // Resource seed
                    QString resourceFilename = resource.value("93M").toString();

                    bool hasName = !item.value("NKm").toString().isEmpty()
                                   || !item.value("fH8").toString().isEmpty()
                                   || !item.value("O=l").toString().isEmpty();
                    bool hasSlots = false;
                    if (hasInventorySlots(item)) {
                        hasSlots = true;
                    } else {
                        QJsonObject inv = item.value(";l5").toObject();
                        QJsonObject cargo = item.value("gan").toObject();
                        QJsonObject tech = item.value("PMT").toObject();
                        hasSlots = hasInventorySlots(inv)
                                   || hasInventorySlots(cargo)
                                   || hasInventorySlots(tech);
                    }
                    bool emptySeed = isExplicitlyEmptySeed(s1) || isExplicitlyEmptySeed(s2) || isExplicitlyEmptySeed(s3);

                    if (!hasName && resourceFilename.isEmpty() && emptySeed && !hasSlots) {
                        continue;
                    }
                } else if (desc.type == InventoryType::Multitool) {
                    QJsonObject mtData = multitoolDataObject(item);
                    QJsonObject store = multitoolStoreObject(mtData);
                    QJsonObject layout = mtData.value("CA4").toObject();
                    QJsonObject resource = mtData.value("NTx").toObject();
                    QJsonValue layoutSeed = layout.value("@EL");
                    QJsonValue resourceSeed = resource.value("@EL");
                    QString resourceFilename = resource.value("93M").toString();
                    bool hasName = !mtData.value("NKm").toString().isEmpty()
                                   || !mtData.value("fH8").toString().isEmpty()
                                   || !mtData.value("O=l").toString().isEmpty()
                                   || !item.value("O=l").toString().isEmpty();
                    bool emptySeed = isExplicitlyEmptySeed(layoutSeed) || isExplicitlyEmptySeed(resourceSeed);
                    bool hasSlots = hasInventorySlots(store);
                    if (!hasName && resourceFilename.isEmpty() && emptySeed && !hasSlots) {
                        continue;
                    }
                }

                QString name;
                if (desc.type == InventoryType::Multitool) {
                    QJsonObject mtData = multitoolDataObject(item);
                    name = mtData.value("NKm").toString();
                    if (name.isEmpty()) name = mtData.value("fH8").toString();
                    if (name.isEmpty()) name = mtData.value("O=l").toString();
                    if (name.isEmpty()) name = item.value("O=l").toString();
                    if (name.isEmpty()) {
                        QJsonObject resource = mtData.value("NTx").toObject();
                        QString filename = resource.value("93M").toString();
                        if (!filename.isEmpty()) {
                            name = QFileInfo(filename).baseName();
                        }
                    }
                } else {
                    name = item.value("NKm").toString(); // Name
                    if (name.isEmpty()) name = item.value("fH8").toString(); // CustomName
                    if (name.isEmpty()) name = item.value("O=l").toString(); // ArchivedName
                }
                if (name.isEmpty()) name = (desc.type == InventoryType::Ship ? tr("Ship %1").arg(i+1) : tr("Multitool %1").arg(i+1));
                
                combo->addItem(name, i);
                
                if (i == currentSelection) {
                    actualIndex = combo->count() - 1;
                }
            }
            
            if (actualIndex >= 0) {
                combo->setCurrentIndex(actualIndex);
            } else if (combo->count() > 0) {
                combo->setCurrentIndex(0);
                int firstValid = combo->itemData(0).toInt();
                if (desc.type == InventoryType::Ship) selectedShipIndex_ = firstValid;
                else selectedMultitoolIndex_ = firstValid;
            }
            
            connect(combo, &QComboBox::currentIndexChanged, this, [this, desc, combo](int index) {
                int originalIndex = combo->itemData(index).toInt();
                if (desc.type == InventoryType::Ship) selectedShipIndex_ = originalIndex;
                else selectedMultitoolIndex_ = originalIndex;
                rebuildTabs();
            });
            
            hbox->addWidget(combo);
            hbox->addStretch();
            vbox->addLayout(hbox);
            vbox->addWidget(scroll);
            tabs_->addTab(container, desc.name);
        } else {
            tabs_->addTab(scroll, desc.name);
        }
    }

    if (sections_.testFlag(InventorySection::Currencies))
    {
        addCurrenciesTab();
    }
    if (sections_.testFlag(InventorySection::Expedition))
    {
        addExpeditionTab();
    }
    if (sections_.testFlag(InventorySection::Settlement))
    {
        addSettlementTab();
    }
    if (sections_.testFlag(InventorySection::StorageManager))
    {
        addStorageManagerTab();
    }

    if (tabs_->count() == 0)
    {
        tabs_->addTab(new QWidget(this), tr("No inventories found"));
    }

    if (tabs_->tabBar())
    {
        tabs_->tabBar()->setVisible(tabs_->count() > 1);
    }
    
    if (currentIndex >= 0 && currentIndex < tabs_->count())
    {
        tabs_->setCurrentIndex(currentIndex);
    }
}

void InventoryEditorPage::updateActiveContext()
{
    usingExpeditionContext_ = false;
    if (!rootDoc_.isObject())
    {
        return;
    }
    QJsonObject root = rootDoc_.object();
    if (!root.contains(kKeyExpeditionContext))
    {
        return;
    }
    QJsonValue contextValue = root.value(kKeyActiveContext);
    QString context = contextValue.toString();
    QString normalized = context.trimmed().toLower();
    if (normalized.isEmpty() || normalized == QString(kContextMain).toLower())
    {
        return;
    }
    QJsonObject expedition = root.value(kKeyExpeditionContext).toObject();
    if (expedition.contains("6f="))
    {
        usingExpeditionContext_ = true;
    }
}

QVariantList InventoryEditorPage::playerBasePath() const
{
    if (usingExpeditionContext_)
    {
        return {kKeyExpeditionContext, "6f="};
    }
    return {kKeyPlayerState, "6f="};
}

bool InventoryEditorPage::resolveExosuit(InventoryDescriptor &out) const
{
    QVariantList basePath = playerBasePath();
    QVariantList inventoryPath = basePath;
    inventoryPath << ";l5";
    QJsonValue inventoryValue = valueAtPath(rootDoc_.object(), inventoryPath);
    if (!inventoryValue.isObject())
    {
        return false;
    }
    out.name = tr("Exosuit");
    out.slotsPath = inventoryPath;
    out.slotsPath << ":No";
    out.validPath = inventoryPath;
    out.validPath << "hl?";
    out.specialSlotsPath = inventoryPath;
    out.specialSlotsPath << "MMm";
    return true;
}

bool InventoryEditorPage::resolveExosuitTech(InventoryDescriptor &out) const
{
    QVariantList basePath = playerBasePath();
    QVariantList inventoryPath = basePath;
    inventoryPath << "PMT";
    QJsonValue inventoryValue = valueAtPath(rootDoc_.object(), inventoryPath);
    if (!inventoryValue.isObject())
    {
        return false;
    }
    out.name = tr("Exosuit Technology");
    out.slotsPath = inventoryPath;
    out.slotsPath << ":No";
    out.validPath = inventoryPath;
    out.validPath << "hl?";
    out.specialSlotsPath = inventoryPath;
    out.specialSlotsPath << "MMm";
    return true;
}

bool InventoryEditorPage::resolveShip(InventoryDescriptor &out) const
{
    QVariantList basePath = playerBasePath();
    QVariantList ownershipPath = basePath;
    ownershipPath << "@Cs";
    QJsonObject player = valueAtPath(rootDoc_.object(), basePath).toObject();
    QJsonArray ownership = valueAtPath(rootDoc_.object(), ownershipPath).toArray();

    int activeIndex = selectedShipIndex_;
    int chosenIndex = -1;

    auto hasSlots = [](const QJsonObject &inv)
    {
        return inv.contains(":No") && inv.value(":No").isArray();
    };

    if (!ownership.isEmpty())
    {
        if (activeIndex < 0 || activeIndex >= ownership.size())
        {
            activeIndex = 0;
        }
        QJsonObject active = ownership.at(activeIndex).toObject();
        QJsonObject inventory = active.contains(":No") ? active : active.value(";l5").toObject();
        if (hasSlots(inventory))
        {
            chosenIndex = activeIndex;
        }
        else
        {
            for (int i = 0; i < ownership.size(); ++i)
            {
                QJsonObject candidate = ownership.at(i).toObject();
                QJsonObject candidateInventory = candidate.contains(":No")
                                                     ? candidate
                                                     : candidate.value(";l5").toObject();
                if (hasSlots(candidateInventory))
                {
                    chosenIndex = i;
                    break;
                }
            }
        }
    }

    if (chosenIndex >= 0)
    {
        QJsonObject ship = ownership.at(chosenIndex).toObject();
        if (ship.contains(":No"))
        {
            out.name = tr("Ship");
            out.type = InventoryType::Ship;
            out.slotsPath = ownershipPath;
            out.slotsPath << chosenIndex << ":No";
            out.validPath = ownershipPath;
            out.validPath << chosenIndex << "hl?";
            out.specialSlotsPath = ownershipPath;
            out.specialSlotsPath << chosenIndex << "MMm";
            return true;
        }
        if (ship.contains(";l5"))
        {
            out.name = tr("Ship");
            out.type = InventoryType::Ship;
            out.slotsPath = ownershipPath;
            out.slotsPath << chosenIndex << ";l5" << ":No";
            out.validPath = ownershipPath;
            out.validPath << chosenIndex << ";l5" << "hl?";
            out.specialSlotsPath = ownershipPath;
            out.specialSlotsPath << chosenIndex << ";l5" << "MMm";
            return true;
        }
    }

    QVariantList legacyPath = basePath;
    legacyPath << "6<E";
    QJsonValue legacyValue = valueAtPath(rootDoc_.object(), legacyPath);
    if (legacyValue.isObject())
    {
        out.name = tr("Ship");
        out.slotsPath = legacyPath;
        out.slotsPath << ":No";
        out.validPath = legacyPath;
        out.validPath << "hl?";
        out.specialSlotsPath = legacyPath;
        out.specialSlotsPath << "MMm";
        return true;
    }

    return false;
}

bool InventoryEditorPage::resolveShipTech(InventoryDescriptor &out) const
{
    QVariantList basePath = playerBasePath();
    QVariantList ownershipPath = basePath;
    ownershipPath << "@Cs";
    QJsonObject player = valueAtPath(rootDoc_.object(), basePath).toObject();
    QJsonArray ownership = valueAtPath(rootDoc_.object(), ownershipPath).toArray();
    if (ownership.isEmpty())
    {
        return false;
    }

    int activeIndex = selectedShipIndex_;
    if (activeIndex < 0 || activeIndex >= ownership.size())
    {
        activeIndex = 0;
    }
    QJsonObject ship = ownership.at(activeIndex).toObject();
    QJsonObject tech = ship.contains("PMT") ? ship.value("PMT").toObject() : ship.value("0wS").toObject();
    if (tech.isEmpty())
    {
        return false;
    }

    out.name = tr("Ship Technology");
    out.type = InventoryType::Ship;
    QString techKey = ship.contains("PMT") ? "PMT" : "0wS";
    out.slotsPath = ownershipPath;
    out.slotsPath << activeIndex << techKey << ":No";
    out.validPath = ownershipPath;
    out.validPath << activeIndex << techKey << "hl?";
    out.specialSlotsPath = ownershipPath;
    out.specialSlotsPath << activeIndex << techKey << "MMm";
    return true;
}


bool InventoryEditorPage::resolveMultitool(InventoryDescriptor &out) const
{
    QVariantList basePath = playerBasePath();
    QVariantList mPath = findMultitoolPath(rootDoc_.object(), basePath);
    QJsonValue mListVal = valueAtPath(rootDoc_.object(), mPath);

    QVariantList inventoryPath = basePath;
    if (mListVal.isArray()) {
        QJsonArray mList = mListVal.toArray();
        if (!mList.isEmpty()) {
            int idx = (selectedMultitoolIndex_ >= 0 && selectedMultitoolIndex_ < mList.size()) ? selectedMultitoolIndex_ : 0;
            inventoryPath = mPath;
            inventoryPath << idx;
            // If it's in a list, it usually has its inventory under ";l5"
            QJsonValue test = valueAtPath(rootDoc_.object(), inventoryPath);
            if (test.isObject()) {
                QJsonObject testObj = test.toObject();
                if (testObj.contains("97S") && testObj.value("97S").isObject()) {
                    inventoryPath << "97S";
                    testObj = testObj.value("97S").toObject();
                }
                if (testObj.contains("OsQ")) {
                    inventoryPath << "OsQ";
                } else if (testObj.contains(";l5")) {
                    inventoryPath << ";l5";
                }
            }
        }
    } else {
        inventoryPath = mPath;
    }
    
    QJsonValue inventoryValue = valueAtPath(rootDoc_.object(), inventoryPath);
    if (!inventoryValue.isObject())
    {
        QVariantList altStore = inventoryPath;
        altStore << "OsQ";
        if (valueAtPath(rootDoc_.object(), altStore).isObject()) {
            inventoryPath = altStore;
        } else {
            QVariantList alt = inventoryPath; alt << ";l5";
            if (valueAtPath(rootDoc_.object(), alt).isObject()) inventoryPath = alt;
            else return false;
        }
    }
    out.name = tr("Multitool");
    out.type = InventoryType::Multitool;
    out.slotsPath = inventoryPath;
    out.slotsPath << ":No";
    out.validPath = inventoryPath;
    out.validPath << "hl?";
    out.specialSlotsPath = inventoryPath;
    out.specialSlotsPath << "MMm";
    return true;
}

bool InventoryEditorPage::resolveMultitoolTech(InventoryDescriptor &out) const
{
    QVariantList basePath = playerBasePath();
    QVariantList mPath = findMultitoolPath(rootDoc_.object(), basePath);
    QJsonValue mListVal = valueAtPath(rootDoc_.object(), mPath);

    QVariantList inventoryPath = basePath;
    if (mListVal.isArray()) {
        QJsonArray mList = mListVal.toArray();
        if (!mList.isEmpty()) {
            int idx = (selectedMultitoolIndex_ >= 0 && selectedMultitoolIndex_ < mList.size()) ? selectedMultitoolIndex_ : 0;
            inventoryPath = mPath;
            inventoryPath << idx;
            QJsonValue test = valueAtPath(rootDoc_.object(), inventoryPath);
            if (test.isObject()) {
                QJsonObject testObj = test.toObject();
                if (testObj.contains("97S") && testObj.value("97S").isObject()) {
                    inventoryPath << "97S";
                    testObj = testObj.value("97S").toObject();
                }
                if (testObj.contains(";l5")) inventoryPath << ";l5";
            }
        }
    } else {
        inventoryPath = mPath;
    }
    inventoryPath << "PMT";

    QJsonValue techValue = valueAtPath(rootDoc_.object(), inventoryPath);
    if (!techValue.isObject())
    {
        InventoryDescriptor fallback;
        if (!resolveMultitool(fallback)) {
            return false;
        }
        out = fallback;
        out.name = tr("Multitool Technology");
        return true;
    }
    out.name = tr("Multitool Technology");
    out.type = InventoryType::Multitool;
    out.slotsPath = inventoryPath;
    out.slotsPath << ":No";
    out.validPath = inventoryPath;
    out.validPath << "hl?";
    out.specialSlotsPath = inventoryPath;
    out.specialSlotsPath << "MMm";
    return true;
}

bool InventoryEditorPage::resolveFreighter(InventoryDescriptor &out) const
{
    QVariantList basePath = playerBasePath();
    QVariantList freighterPath = basePath;
    freighterPath << "D3F";
    QJsonValue freighterValue = valueAtPath(rootDoc_.object(), freighterPath);
    QJsonObject freighter = freighterValue.toObject();
    if (freighter.isEmpty())
    {
        return false;
    }
    if (!freighter.contains(":No"))
    {
        return false;
    }

    out.name = tr("Freighter");
    out.slotsPath = freighterPath;
    out.slotsPath << ":No";
    out.specialSlotsPath = freighterPath;
    out.specialSlotsPath << "MMm";

    QString validKey = ":Nq";
    if (freighter.contains(":Nq"))
    {
        validKey = ":Nq";
    }
    else
    {
        for (auto it = freighter.begin(); it != freighter.end(); ++it)
        {
            if (!it.value().isArray())
            {
                continue;
            }
            QJsonArray arr = it.value().toArray();
            if (arr.isEmpty())
            {
                continue;
            }
            QJsonObject first = arr.at(0).toObject();
            if (first.contains("=Tb") && first.contains("N9>"))
            {
                validKey = it.key();
                break;
            }
        }
    }
    out.validPath = freighterPath;
    out.validPath << validKey;
    return true;
}

bool InventoryEditorPage::resolveCorvetteCache(InventoryDescriptor &out) const
{
    QVariantList basePath = playerBasePath();
    QVariantList inventoryPath = basePath;
    inventoryPath << "wem";
    QJsonValue inventoryValue = valueAtPath(rootDoc_.object(), inventoryPath);
    QString slotsKey = ":No";
    QString validKey = "hl?";
    QString specialKey = "MMm";
    if (!inventoryValue.isObject()) {
        inventoryPath = basePath;
        inventoryPath << "CorvetteStorageInventory";
        inventoryValue = valueAtPath(rootDoc_.object(), inventoryPath);
        slotsKey = "Slots";
        validKey = "ValidSlotIndices";
        specialKey = "SpecialSlots";
    }
    if (!inventoryValue.isObject()) {
        return false;
    }
    QJsonObject inventoryObj = inventoryValue.toObject();
    if (!inventoryObj.contains(slotsKey)) {
        return false;
    }
    out.name = tr("Corvette Cache");
    out.slotsPath = inventoryPath;
    out.slotsPath << slotsKey;
    out.validPath = inventoryPath;
    out.validPath << validKey;
    out.specialSlotsPath = inventoryPath;
    out.specialSlotsPath << specialKey;
    return true;
}

QJsonValue InventoryEditorPage::valueAtPath(const QJsonValue &root, const QVariantList &path)
{
    QJsonValue result = root;
    for (const QVariant &p : path)
    {
        if (result.isObject())
        {
            result = result.toObject().value(p.toString());
        }
        else if (result.isArray())
        {
            result = result.toArray().at(p.toInt());
        }
        else
        {
            return QJsonValue();
        }
    }
    return result;
}

QJsonValue InventoryEditorPage::setValueAtPath(const QJsonValue &root, const QVariantList &path,
                                               int depth, const QJsonValue &value)
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

void InventoryEditorPage::applyValueAtPath(const QVariantList &path, const QJsonValue &value)
{
    QJsonValue rootValue = rootDoc_.isObject() ? QJsonValue(rootDoc_.object())
                                               : QJsonValue(rootDoc_.array());
    if (valueAtPath(rootValue, path) == value) {
        return;
    }
    QJsonValue updated = setValueAtPath(rootValue, path, 0, value);
    if (updated.isObject()) {
        rootDoc_.setObject(updated.toObject());
    } else if (updated.isArray()) {
        rootDoc_.setArray(updated.toArray());
    }
    if (losslessDoc_) {
        losslessDoc_->setValueAtPath(path, value);
    }
    hasUnsavedChanges_ = true;
}

void InventoryEditorPage::applyDiffAtPath(const QVariantList &path, const QJsonValue &current,
                                          const QJsonValue &updated)
{
    if (current == updated) {
        return;
    }
    if (current.isObject() && updated.isObject()) {
        QJsonObject currentObj = current.toObject();
        QJsonObject updatedObj = updated.toObject();
        for (auto it = updatedObj.begin(); it != updatedObj.end(); ++it) {
            QJsonValue currentValue = currentObj.value(it.key());
            applyDiffAtPath(path + QVariantList{it.key()}, currentValue, it.value());
        }
        return;
    }
    if (current.isArray() && updated.isArray()) {
        QJsonArray currentArr = current.toArray();
        QJsonArray updatedArr = updated.toArray();
        if (currentArr.size() != updatedArr.size()) {
            applyValueAtPath(path, updated);
            return;
        }
        for (int i = 0; i < updatedArr.size(); ++i) {
            applyDiffAtPath(path + QVariantList{i}, currentArr.at(i), updatedArr.at(i));
        }
        return;
    }
    applyValueAtPath(path, updated);
}

QJsonObject InventoryEditorPage::activePlayerState() const
{
    QVariantList basePath = playerBasePath();
    return valueAtPath(rootDoc_.object(), basePath).toObject();
}

QJsonArray InventoryEditorPage::ensureMilestoneArray(QJsonObject &seasonState,
                                                     int requiredSize) const
{
    QJsonArray milestoneValues = seasonState.value(kKeyMilestoneValues).toArray();
    while (milestoneValues.size() < requiredSize)
    {
        milestoneValues.append(0);
    }
    seasonState.insert(kKeyMilestoneValues, milestoneValues);
    return milestoneValues;
}

void InventoryEditorPage::addCurrenciesTab()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    auto *grid = new QGridLayout();
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(12);

    QVariantList playerPath = playerBasePath();
    QJsonObject playerState = valueAtPath(rootDoc_.object(), playerPath).toObject();

    struct CurrencyDef
    {
        QString label;
        QString key;
        QString icon;
    };
    QList<CurrencyDef> currencies = {
        {tr("Units"), kKeyUnits, kIconUnits},
        {tr("Nanites"), kKeyNanites, kIconNanites},
        {tr("Quicksilver"), kKeyQuicksilver, kIconQuicksilver}};

    for (int i = 0; i < currencies.size(); ++i)
    {
        const auto &def = currencies.at(i);
        QPixmap icon = IconRegistry::iconForId(def.icon);
        if (!icon.isNull())
        {
            auto *iconLabel = new QLabel(page);
            iconLabel->setPixmap(icon.scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            grid->addWidget(iconLabel, i, 0);
        }

        auto *label = new QLabel(def.label, page);
        grid->addWidget(label, i, 1);

        auto *field = new QLineEdit(page);
        field->setFixedWidth(140);
        qint64 initialValue = playerState.value(def.key).toVariant().toLongLong();
        field->setText(QLocale::system().toString(initialValue));
        grid->addWidget(field, i, 2);

        connect(field, &QLineEdit::editingFinished, this, [this, field, def, playerPath]()
                {
            bool ok = false;
            QString text = field->text();
            qint64 value = QLocale::system().toLongLong(text, &ok);
            if (!ok) {
                value = text.toLongLong(&ok);
                if (!ok) return;
            }
            
            applyValueAtPath(playerPath + QVariantList{def.key}, value);
            field->setText(QLocale::system().toString(value));
            emit statusMessage(tr("Pending changes — remember to Save!")); });
    }

    auto *container = new QWidget(page);
    container->setFixedWidth(400);
    container->setLayout(grid);
    layout->addWidget(container);

    auto *saveButton = new QPushButton(tr("Save Changes"), page);
    saveButton->setFixedWidth(200);
    layout->addWidget(saveButton);
    connect(saveButton, &QPushButton::clicked, this, [this]()
            {
        QString error;
        if (saveChanges(&error)) {
            emit statusMessage(tr("Changes saved successfully."));
        } else {
            emit statusMessage(tr("Error saving changes: %1").arg(error));
        } });

    layout->addStretch();
    tabs_->addTab(page, tr("Currencies"));
}

void InventoryEditorPage::addExpeditionTab()
{
    if (!rootDoc_.isObject())
    {
        return;
    }
    QJsonObject root = rootDoc_.object();
    QJsonObject commonState = root.value(kKeyCommonState).toObject();
    if (commonState.isEmpty())
    {
        return;
    }
    QJsonObject seasonData = commonState.value(kKeySeasonData).toObject();
    QJsonArray stages = seasonData.value(kKeySeasonStages).toArray();
    if (stages.isEmpty())
    {
        return;
    }

    QJsonObject seasonState = commonState.value(kKeySeasonState).toObject();

    int totalMilestones = 0;
    for (const QJsonValue &stageValue : stages)
    {
        QJsonObject stage = stageValue.toObject();
        QJsonArray milestones = stage.value(kKeyStageMilestones).toArray();
        totalMilestones += milestones.size();
    }
    if (totalMilestones == 0)
    {
        return;
    }

    QJsonArray milestoneValues = ensureMilestoneArray(seasonState, totalMilestones);
    commonState.insert(kKeySeasonState, seasonState);
    root.insert(kKeyCommonState, commonState);
    rootDoc_.setObject(root);
    applyValueAtPath({kKeyCommonState, kKeySeasonState, kKeyMilestoneValues}, milestoneValues);

    auto *content = new QWidget(this);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(16);

    int milestoneOffset = 0;
    int stageCount = qMin(5, stages.size());
    QVariantList milestonePath = {kKeyCommonState, kKeySeasonState, kKeyMilestoneValues};

    for (int i = 0; i < stageCount; ++i)
    {
        QJsonObject stage = stages.at(i).toObject();
        QJsonArray milestones = stage.value(kKeyStageMilestones).toArray();
        bool showCompleteAll = true;
        QWidget *section = buildExpeditionStage(stage, milestones, milestoneValues, milestonePath,
                                                i, milestoneOffset, showCompleteAll);
        layout->addWidget(section);
        milestoneOffset += milestones.size();
    }

    layout->addStretch();

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setWidget(content);
    tabs_->addTab(scroll, tr("Expedition"));
}

QWidget *InventoryEditorPage::buildExpeditionStage(const QJsonObject &stage, const QJsonArray &milestones,
                                                   QJsonArray &milestoneValues,
                                                   const QVariantList &milestonePath,
                                                   int stageIndex, int milestoneStart, bool showCompleteAll)
{
    auto *group = new QGroupBox(this);
    QString stageName = formatExpeditionToken(stage.value("8wT").toString());
    QString title = tr("Stage %1").arg(stageIndex + 1);
    if (!stageName.isEmpty())
    {
        title += tr(" – %1").arg(stageName);
    }
    group->setTitle(title);
    group->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);

    auto *layout = new QGridLayout(group);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setHorizontalSpacing(4);
    layout->setVerticalSpacing(4);
    layout->setSizeConstraint(QLayout::SetFixedSize);

    layout->setColumnMinimumWidth(0, 48);
    layout->setColumnMinimumWidth(1, 200);
    layout->setColumnMinimumWidth(2, 40);
    layout->setColumnMinimumWidth(3, 110);
    layout->setColumnMinimumWidth(4, 40);

    int headerRow = 0;
    int firstMilestoneRow = 1;
    if (showCompleteAll) {
        headerRow = 1;
        firstMilestoneRow = 2;
    }

    layout->addWidget(new QLabel(tr("Mission"), group), headerRow, 1);
    layout->addWidget(new QLabel(tr("Goal"), group), headerRow, 2, Qt::AlignLeft);
    layout->addWidget(new QLabel(tr("Progress"), group), headerRow, 3, Qt::AlignLeft);
    layout->addWidget(new QLabel(tr("Done"), group), headerRow, 4, Qt::AlignLeft);

    struct MilestoneWidgets
    {
        QLineEdit *progressField = nullptr;
        QCheckBox *doneCheck = nullptr;
        int goalValue = 0;
        int milestoneIndex = -1;
    };

    QVector<MilestoneWidgets> stageWidgets;
    stageWidgets.reserve(milestones.size());
    QVector<int> stageGoals;
    stageGoals.reserve(milestones.size());

    for (int i = 0; i < milestones.size(); ++i)
    {
        QJsonObject milestone = milestones.at(i).toObject();
        int row = i + firstMilestoneRow;
        int milestoneIndex = milestoneStart + i;

        QLabel *iconLabel = new QLabel(group);
        QJsonObject iconObj = milestone.value(kKeyIcon).toObject();
        QString iconFilename = iconObj.value(kKeyIconFilename).toString();
        QString normalized = iconFilename;
        normalized.replace("\\\\", "/");
        int slash = normalized.lastIndexOf('/');
        if (slash >= 0)
        {
            normalized = normalized.mid(slash + 1);
        }
        QString pngName = normalized;
        pngName.replace(".DDS", ".png", Qt::CaseInsensitive);
        if (!pngName.isEmpty())
        {
            QString path = QString("icons/expedition/%1").arg(pngName.toLower());
            QPixmap icon = QPixmap(ResourceLocator::resolveResource(path));
            if (!icon.isNull())
            {
                iconLabel->setPixmap(icon.scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            }
        }
        layout->addWidget(iconLabel, row, 0);

        QString missionName = formatExpeditionToken(milestone.value(kKeyMissionName).toString());
        if (missionName.isEmpty())
        {
            missionName = tr("Mission %1").arg(i + 1);
        }
        layout->addWidget(new QLabel(missionName, group), row, 1);

        int goalValue = static_cast<int>(milestone.value(kKeyMissionAmount).toDouble(0));
        layout->addWidget(new QLabel(formatQuantity(goalValue), group), row, 2);

        auto *progressField = new QLineEdit(group);
        progressField->setValidator(new QIntValidator(0, 9999999, progressField));
        progressField->setMaxLength(10);
        const int fieldWidth = progressField->fontMetrics().horizontalAdvance(QStringLiteral("0000000000")) + 16;
        progressField->setFixedWidth(fieldWidth);
        double stored = 0.0;
        if (milestoneIndex >= 0 && milestoneIndex < milestoneValues.size())
        {
            stored = milestoneValues.at(milestoneIndex).toDouble(0);
        }
        progressField->setText(formatQuantity(stored));
        layout->addWidget(progressField, row, 3);

        auto *doneCheck = new QCheckBox(group);
        doneCheck->setChecked(qFuzzyCompare(stored + 1.0, goalValue + 1.0));
        layout->addWidget(doneCheck, row, 4);

        auto commitValue = [this, progressField, milestoneIndex, milestonePath]()
        {
            bool ok = false;
            int value = progressField->text().toInt(&ok);
            if (!ok)
            {
                return;
            }
            QJsonArray values = valueAtPath(rootDoc_.object(), milestonePath).toArray();
            if (milestoneIndex < 0 || milestoneIndex >= values.size())
            {
                return;
            }
            values.replace(milestoneIndex, value);
            applyValueAtPath(milestonePath, values);
            emit statusMessage(tr("Pending changes — remember to Save!"));
        };

        connect(progressField, &QLineEdit::editingFinished, this, [commitValue]()
                { commitValue(); });

        connect(doneCheck, &QCheckBox::toggled, this, [this, progressField, goalValue, commitValue]()
                {
            if (goalValue <= 0) {
                return;
            }
            if (progressField->text().isEmpty()) {
                progressField->setText("0");
            }
            if (static_cast<int>(goalValue) != progressField->text().toInt()) {
                progressField->setText(QString::number(static_cast<int>(goalValue)));
                commitValue();
            } });

        MilestoneWidgets widgets;
        widgets.progressField = progressField;
        widgets.doneCheck = doneCheck;
        widgets.goalValue = goalValue;
        widgets.milestoneIndex = milestoneIndex;
        stageWidgets.append(widgets);
        stageGoals.append(goalValue);
    }

    if (showCompleteAll && !stageWidgets.isEmpty())
    {
        bool allComplete = true;
        for (const auto &widgets : stageWidgets)
        {
            if (!widgets.doneCheck->isChecked())
            {
                allComplete = false;
                break;
            }
        }

        auto *completeAll = new QCheckBox(tr("Complete All"), group);
        completeAll->setChecked(allComplete);
        layout->addWidget(completeAll, 0, 3, 1, 2, Qt::AlignRight);

        connect(completeAll, &QCheckBox::toggled, this, [this, stageWidgets, stageGoals, milestonePath](bool checked)
                {
            QJsonArray values = valueAtPath(rootDoc_.object(), milestonePath).toArray();
            bool updated = false;

            int lastIndex = stageWidgets.size() - 1;
            for (int i = 0; i < stageWidgets.size(); ++i) {
                if (i == lastIndex) {
                    continue;
                }
                const auto &widgets = stageWidgets.at(i);
                if (widgets.milestoneIndex < 0 || widgets.milestoneIndex >= values.size()) {
                    continue;
                }
                int goal = stageGoals.value(i, widgets.goalValue);
                if (goal <= 0) {
                    continue;
                }
                int nextValue = checked ? goal : 0;
                values.replace(widgets.milestoneIndex, nextValue);
                QSignalBlocker blockProgress(widgets.progressField);
                QSignalBlocker blockDone(widgets.doneCheck);
                widgets.progressField->setText(QString::number(nextValue));
                widgets.doneCheck->setChecked(checked);
                updated = true;
            }

            if (!updated) {
                return;
            }
            applyValueAtPath(milestonePath, values);
            emit statusMessage(tr("Pending changes — remember to Save!")); });
    }

    return group;
}

void InventoryEditorPage::addSettlementTab()
{
    QJsonObject settlement = settlementRoot();
    if (settlement.isEmpty())
    {
        return;
    }
    auto *content = buildSettlementForm(settlement);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setWidget(content);
    tabs_->addTab(scroll, tr("Settlement"));
}

QJsonObject InventoryEditorPage::settlementRoot() const
{
    if (!rootDoc_.isObject())
    {
        return {};
    }
    QJsonObject root = rootDoc_.object();
    QJsonObject settlementLocal = root.value(kKeySettlementLocalData).toObject();
    if (settlementLocal.isEmpty())
    {
        QJsonObject player = activePlayerState();
        settlementLocal = player.value(kKeySettlementLocalData).toObject();
    }
    QJsonArray states = settlementLocal.value(kKeySettlementStates).toArray();
    if (states.isEmpty())
    {
        return {};
    }
    return states.at(0).toObject();
}

QWidget *InventoryEditorPage::buildSettlementForm(QJsonObject &settlement)
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    auto *grid = new QGridLayout();
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(10);

    QVariantList settlementPath;
    QJsonObject root = rootDoc_.object();
    if (root.value(kKeySettlementLocalData).isObject())
    {
        settlementPath = {kKeySettlementLocalData, kKeySettlementStates, 0};
    }
    else
    {
        QVariantList basePath = playerBasePath();
        settlementPath = basePath;
        settlementPath << kKeySettlementLocalData << kKeySettlementStates << 0;
    }

    auto updateSettlementField = [this, settlementPath](const QString &key, const QJsonValue &value)
    {
        applyValueAtPath(settlementPath + QVariantList{key}, value);
        emit statusMessage(tr("Pending changes — remember to Save!"));
    };

    int row = 0;
    auto *nameLabel = new QLabel(tr("Settlement Name:"), page);
    auto *nameField = new QLineEdit(page);
    nameField->setText(settlement.value(kKeySettlementName).toString());
    grid->addWidget(nameLabel, row, 0);
    grid->addWidget(nameField, row, 1);
    connect(nameField, &QLineEdit::editingFinished, this, [nameField, updateSettlementField]()
            { updateSettlementField(kKeySettlementName, nameField->text()); });
    row++;

    auto addNumericRow = [this, page, grid, &row, settlementPath](const QString &labelText,
                                                                  const QString &key)
    {
        auto *label = new QLabel(labelText, page);
        auto *field = new QLineEdit(page);
        field->setValidator(new QIntValidator(0, 9999999, field));
        field->setText(QString::number(valueAtPath(rootDoc_.object(), settlementPath + QVariantList{key}).toVariant().toLongLong()));
        grid->addWidget(label, row, 0);
        grid->addWidget(field, row, 1);
        connect(field, &QLineEdit::editingFinished, this, [this, field, settlementPath, key]()
                {
            bool ok = false;
            qint64 value = field->text().toLongLong(&ok);
            if (!ok) {
                return;
            }
            applyValueAtPath(settlementPath + QVariantList{key}, value);
            emit statusMessage(tr("Pending changes — remember to Save!")); });
        row++;
    };

    addNumericRow(tr("Population:"), kKeySettlementPopulation);

    QJsonArray stats = settlement.value(kKeySettlementStats).toArray();
    for (int i = 0; i < stats.size(); ++i)
    {
        QJsonObject stat = stats.at(i).toObject();
        QString statId = stat.value(kKeySettlementStatId).toString();
        QString displayName = formatStatId(statId);
        auto *label = new QLabel(displayName + ":", page);
        auto *field = new QLineEdit(page);
        field->setValidator(new QIntValidator(0, 9999999, field));
        field->setText(QString::number(stat.value(kKeySettlementValue).toVariant().toLongLong()));
        grid->addWidget(label, row, 0);
        grid->addWidget(field, row, 1);

        QVariantList statPath = settlementPath;
        statPath << kKeySettlementStats << i << kKeySettlementValue;
        connect(field, &QLineEdit::editingFinished, this, [this, field, statPath]()
                {
            bool ok = false;
            qint64 value = field->text().toLongLong(&ok);
            if (!ok) {
                return;
            }
            applyValueAtPath(statPath, value);
            emit statusMessage(tr("Pending changes — remember to Save!")); });
        row++;
    }

    addNumericRow(tr("Alert Level:"), "A<w");
    addNumericRow(tr("Sentinel Attacks:"), "A<w");
    addNumericRow(tr("Settler Deaths:"), "qr=");
    addNumericRow(tr("Bug Attacks:"), "oCR");
    addNumericRow(tr("Judgements Settled:"), "9=d");

    layout->addLayout(grid);
    layout->addStretch();
    return page;
}

void InventoryEditorPage::addStorageManagerTab()
{
    QWidget *page = buildStorageManager();
    if (!page)
    {
        return;
    }
    tabs_->addTab(page, tr("Storage Manager"));
}

QWidget *InventoryEditorPage::buildStorageManager()
{
    QVariantList basePath = playerBasePath();
    QStringList chestKeys = {"3Nc", "IDc", "M=:", "iYp", "<IP", "qYJ", "@e5", "5uh", "5Tg", "Bq<"};

    QList<InventoryDescriptor> containers;
    for (int i = 0; i < chestKeys.size(); ++i)
    {
        QVariantList containerPath = basePath;
        containerPath << chestKeys.at(i);
        QJsonValue containerValue = valueAtPath(rootDoc_.object(), containerPath);
        if (!containerValue.isObject())
        {
            continue;
        }
        QJsonObject container = containerValue.toObject();
        if (!container.contains(":No"))
        {
            continue;
        }
        InventoryDescriptor desc;
        desc.name = tr("Storage Container %1").arg(i);
        desc.slotsPath = containerPath;
        desc.slotsPath << ":No";
        desc.validPath = containerPath;
        desc.validPath << "hl?";
        containers.append(desc);
    }

    if (containers.isEmpty())
    {
        return nullptr;
    }

    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    auto *row = new QHBoxLayout();
    row->setSpacing(6);
    auto *combo = new QComboBox(page);
    for (const InventoryDescriptor &desc : containers)
    {
        combo->addItem(desc.name);
    }
    auto *openButton = new QPushButton(tr("Open Container"), page);
    row->addWidget(new QLabel(tr("Select Container:"), page));
    row->addWidget(combo);
    row->addWidget(openButton);
    row->addStretch();
    layout->addLayout(row);

    const int searchWidth = 500;
    const int searchButtonWidth = 84;
    auto *searchPane = new QWidget(page);
    searchPane->setFixedWidth(searchWidth);
    searchPane->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

    auto *searchLayout = new QVBoxLayout(searchPane);
    searchLayout->setContentsMargins(0, 0, 0, 0);
    searchLayout->setSpacing(6);

    auto *searchLabel = new QLabel(tr("Global Item Search"), searchPane);
    searchLayout->addWidget(searchLabel);

    auto *searchRow = new QHBoxLayout();
    searchRow->setSpacing(6);
    auto *searchField = new QLineEdit(searchPane);
    searchField->setPlaceholderText(tr("Search item name..."));
    auto *searchButton = new QPushButton(tr("Search"), searchPane);
    searchButton->setFixedWidth(searchButtonWidth);
    searchField->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    const int fieldWidth =
        searchWidth - searchButtonWidth - searchRow->spacing() - searchLayout->contentsMargins().left() - searchLayout->contentsMargins().right();
    searchField->setFixedWidth(fieldWidth);
    searchRow->addWidget(searchField);
    searchRow->addWidget(searchButton);
    searchLayout->addLayout(searchRow);

    auto *results = new QListWidget(searchPane);
    results->setMinimumHeight(240);
    results->setFixedWidth(searchWidth);
    searchLayout->addWidget(results);

    layout->addWidget(searchPane, 0, Qt::AlignLeft);

    auto openContainer = [this, containers](int index)
    {
        if (index < 0 || index >= containers.size())
        {
            return;
        }
        const InventoryDescriptor &desc = containers.at(index);
        QJsonArray slotsArray = valueAtPath(rootDoc_.object(), desc.slotsPath).toArray();
        QJsonArray valid = valueAtPath(rootDoc_.object(), desc.validPath).toArray();

        auto *window = new QMainWindow(this->window());
        window->setAttribute(Qt::WA_DeleteOnClose, true);
        window->setAttribute(Qt::WA_QuitOnClose, false);
        window->setWindowTitle(desc.name);
        auto *grid = new InventoryGridWidget(window);
        grid->setInventory(desc.name, slotsArray, valid);
        grid->setCommitHandler([this, desc](const QJsonArray &updatedSlots, const QJsonArray &)
                               {
            QJsonValue currentSlots = valueAtPath(rootDoc_.object(), desc.slotsPath);
            applyDiffAtPath(desc.slotsPath, currentSlots, updatedSlots); });
        connect(grid, &InventoryGridWidget::statusMessage, this, &InventoryEditorPage::statusMessage);
        auto *scroll = new QScrollArea(window);
        scroll->setWidgetResizable(true);
        scroll->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
        scroll->setWidget(grid);
        window->setCentralWidget(scroll);
        const QSize gridSize = grid->minimumSize();
        const int width = qMax(1120, gridSize.width() + 80);
        const int height = qMax(720, gridSize.height() + 140);
        window->resize(width, height);
        window->show();
    };

    auto runSearch = [this, results, containers, searchField]()
    {
        results->clear();
        QString query = searchField->text().trimmed().toLower();
        if (query.isEmpty())
        {
            return;
        }
        for (int i = 0; i < containers.size(); ++i)
        {
            const InventoryDescriptor &desc = containers.at(i);
            QJsonArray slotsArray = valueAtPath(rootDoc_.object(), desc.slotsPath).toArray();
            for (const QJsonValue &value : slotsArray)
            {
                QJsonObject obj = value.toObject();
                QString id = obj.value("b2n").toString();
                QString name = ItemDefinitionRegistry::displayNameForId(id);
                if (name.isEmpty())
                {
                    name = id;
                }
                QString combined = QString("%1 (%2)").arg(name, id);
                if (combined.toLower().contains(query))
                {
                    int amount = obj.value("1o9").toInt();
                    QString label = QString("%1 (%2) in %3").arg(combined).arg(amount).arg(desc.name);
                    auto *item = new QListWidgetItem(label, results);
                    item->setData(Qt::UserRole, i);
                    QPixmap icon = IconRegistry::iconForId(id);
                    if (!icon.isNull())
                    {
                        item->setIcon(QIcon(icon));
                    }
                }
            }
        }
        if (results->count() == 0)
        {
            results->addItem(new QListWidgetItem(tr("No results found in storage containers."), results));
        }
    };

    connect(openButton, &QPushButton::clicked, this, [combo, openContainer]()
            { openContainer(combo->currentIndex()); });
    connect(searchButton, &QPushButton::clicked, this, runSearch);
    connect(searchField, &QLineEdit::textChanged, this, runSearch);
    connect(results, &QListWidget::itemDoubleClicked, this, [openContainer](QListWidgetItem *item)
            {
        bool ok = false;
        int index = item->data(Qt::UserRole).toInt(&ok);
        if (ok) {
            openContainer(index);
        } });

    return page;
}

QString InventoryEditorPage::formatStatId(const QString &id) const
{
    if (id == "SETTLE_HAPP")
    {
        return tr("Happiness");
    }
    if (id == "SETTLE_PROD")
    {
        return tr("Productivity");
    }
    if (id == "SETTLE_MAINT")
    {
        return tr("Upkeep");
    }
    if (id == "SETTLE_DEBT")
    {
        return tr("Debt");
    }
    return id;
}

QString InventoryEditorPage::formatExpeditionToken(const QString &raw) const
{
    if (raw.isEmpty())
    {
        return QString();
    }
    QString value = raw;
    if (value.startsWith('^'))
    {
        value = value.mid(1);
    }
    value.replace('_', ' ');
    return value.trimmed();
}

QString InventoryEditorPage::formatQuantity(double value) const
{
    double rounded = std::rint(value);
    if (qAbs(value - rounded) < 0.0001)
    {
        return QString::number(static_cast<qint64>(rounded));
    }
    return QString::number(value);
}
