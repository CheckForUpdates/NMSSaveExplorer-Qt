#include "settlement/SettlementManagerPage.h"

#include "core/SaveDecoder.h"
#include "core/SaveEncoder.h"
#include "registry/ItemDefinitionRegistry.h"

#include <climits>
#include <functional>
#include <QComboBox>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {
const char *kKeyActiveContext = "XTp";
const char *kKeyExpeditionContext = "2YS";
const char *kKeyPlayerState = "vLc";
const char *kContextMain = "Main";
const char *kKeyCommonState = "<h0";
const char *kKeyCommonStateLong = "CommonStateData";
const char *kKeyDiscoveryManager = "fDu";
const char *kKeyDiscoveryManagerLong = "DiscoveryManagerData";
const char *kKeyDiscoveryData = "ETO";
const char *kKeyDiscoveryDataLong = "DiscoveryData-v1";
const char *kKeySettlementLocalData = "NEK";
const char *kKeySettlementStates = "GQA";
const char *kKeySettlementLocalDataLong = "SettlementLocalSaveData";
const char *kKeySettlementStatesLong = "SettlementStatesV2";
const char *kKeySettlementStats = "@bB";
const char *kKeySettlementStatId = "QL1";
const char *kKeySettlementValue = ">MX";
const char *kKeySettlementPopulation = "x3<";
const char *kKeySettlementName = "NKm";
const char *kKeySettlementPopulationLong = "Population";
const char *kKeySettlementNameLong = "Name";
const char *kKeySettlementOwner = "3?K";
const char *kKeySettlementOwnerLong = "Owner";
const char *kKeyUsername = "OL5";
const char *kKeyUsernameLong = "Username";
const char *kKeyOwnerLid = "f5Q";
const char *kKeyOwnerUid = "K7E";
const char *kKeyOwnerUsn = "V?:";
const char *kKeyOwnerPtk = "D6b";
const char *kKeyOwnerLidLong = "LID";
const char *kKeyOwnerUidLong = "UID";
const char *kKeyOwnerUsnLong = "USN";
const char *kKeyOwnerPtkLong = "PTK";
const char *kKeyUsedDiscoveryOwners = "F=J";
const char *kKeyUsedDiscoveryOwnersLong = "UsedDiscoveryOwnersV2";
const char *kKeyPersistentBases = "F?0";
const char *kKeyPersistentBasesLong = "PersistentPlayerBases";
const char *kKeySettlementSeed = "BKy";
const char *kKeySeedValue = "qK9";
const char *kKeySeedValueLong = "SeedValue";
const char *kKeyPendingDecision = "HMQ";
const char *kKeyPendingDecisionLong = "PendingJudgementType";
const char *kKeyLastDecision = "?SU";
const char *kKeyLastDecisionLong = "SettlementJudgementType";
const char *kKeyJudgementTypeLong = "SettlementJudgementType";
const char *kKeyJudgementTypeShort = "?SU";
const char *kKeyLastDecisionTime = "0Qr";
const char *kKeyLastDecisionTimeLong = "LastJudgementTime";
const char *kKeySentinelAttackCount = "A<w";
const char *kKeySettlerDeathCount = "qr=";
const char *kKeyBugAttackCount = "oCR";
const char *kKeyJudgementsSettled = "9=d";
const char *kKeySettlementStatsLong = "Stats";
const char *kKeySettlementStatsShort = "gUR";
const char *kKeySettlementPerks = "OEf";
const char *kKeySettlementPerksLong = "Perks";
}

SettlementManagerPage::SettlementManagerPage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

bool SettlementManagerPage::loadFromFile(const QString &filePath, QString *errorMessage)
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
    resolveSettlementStatesPath();
    rebuildSettlementList();
    return true;
}

bool SettlementManagerPage::hasLoadedSave() const
{
    return !rootDoc_.isNull();
}

bool SettlementManagerPage::hasUnsavedChanges() const
{
    return hasUnsavedChanges_;
}

const QString &SettlementManagerPage::currentFilePath() const
{
    return currentFilePath_;
}

bool SettlementManagerPage::saveChanges(QString *errorMessage)
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

void SettlementManagerPage::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);

    auto *row = new QHBoxLayout();
    row->setSpacing(8);
    row->addWidget(new QLabel(tr("Owned Settlements:"), this));
    settlementCombo_ = new QComboBox(this);
    settlementCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    row->addWidget(settlementCombo_);
    row->addStretch();
    layout->addLayout(row);

    scrollArea_ = new QScrollArea(this);
    scrollArea_->setWidgetResizable(true);
    layout->addWidget(scrollArea_);

    auto *saveButton = new QPushButton(tr("Save Changes"), this);
    saveButton->setFixedWidth(200);
    layout->addWidget(saveButton, 0, Qt::AlignLeft);
    connect(saveButton, &QPushButton::clicked, this, [this]() {
        QString error;
        if (saveChanges(&error)) {
            emit statusMessage(tr("Changes saved successfully."));
        } else {
            emit statusMessage(tr("Error saving changes: %1").arg(error));
        }
    });

    connect(settlementCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) {
                if (index < 0 || index >= settlements_.size())
                {
                    setActiveSettlement(-1);
                    return;
                }
                setActiveSettlement(settlements_.at(index).index);
            });

    setActiveSettlement(-1);
}

void SettlementManagerPage::rebuildSettlementList()
{
    settlementCombo_->blockSignals(true);
    settlementCombo_->clear();
    settlements_.clear();

    if (settlementStatesPath_.isEmpty())
    {
        setActiveSettlement(-1);
        settlementCombo_->blockSignals(false);
        return;
    }

    QList<SettlementEntry> owned = collectOwnedSettlements();
    if (owned.isEmpty())
    {
        QJsonArray states = valueAtPath(rootDoc_.object(), settlementStatesPath()).toArray();
        for (int i = 0; i < states.size(); ++i)
        {
            QJsonObject settlement = states.at(i).toObject();
            SettlementEntry entry;
            entry.index = i;
            entry.name = settlement.value(kKeySettlementName).toString();
            if (entry.name.isEmpty())
            {
                entry.name = tr("Settlement %1").arg(i);
            }
            settlements_.append(entry);
        }
        emit statusMessage(tr("No owned settlements matched; showing all."));
    }
    else
    {
        settlements_ = owned;
    }

    for (const SettlementEntry &entry : settlements_)
    {
        QString label = tr("[%1] %2").arg(entry.index).arg(entry.name);
        settlementCombo_->addItem(label);
    }

    settlementCombo_->blockSignals(false);
    if (!settlements_.isEmpty())
    {
        settlementCombo_->setCurrentIndex(0);
        setActiveSettlement(settlements_.at(0).index);
    }
    else
    {
        setActiveSettlement(-1);
    }
}

void SettlementManagerPage::setActiveSettlement(int index)
{
    if (formWidget_)
    {
        formWidget_->deleteLater();
        formWidget_ = nullptr;
    }
    formWidget_ = buildSettlementForm(index);
    scrollArea_->setWidget(formWidget_);
}

QWidget *SettlementManagerPage::buildSettlementForm(int index)
{
    if (index < 0)
    {
        auto *empty = new QWidget(this);
        auto *layout = new QVBoxLayout(empty);
        layout->addWidget(new QLabel(tr("No settlement data found."), empty));
        layout->addStretch();
        return empty;
    }

    QJsonObject settlement = settlementAtIndex(index);
    if (settlement.isEmpty())
    {
        auto *empty = new QWidget(this);
        auto *layout = new QVBoxLayout(empty);
        layout->addWidget(new QLabel(tr("No settlement data found."), empty));
        layout->addStretch();
        return empty;
    }

    QVariantList settlementPath = settlementStatesPath_;
    settlementPath << index;

    auto updateSettlement = [this, settlementPath](const std::function<void(QJsonObject &)> &mutator)
    {
        QJsonObject settlementObj = valueAtPath(rootDoc_.object(), settlementPath).toObject();
        if (settlementObj.isEmpty())
        {
            return;
        }
        QJsonObject original = settlementObj;
        mutator(settlementObj);
        if (settlementObj == original)
        {
            return;
        }
        for (auto it = settlementObj.begin(); it != settlementObj.end(); ++it)
        {
            if (original.value(it.key()) != it.value())
            {
                applyValueAtPath(settlementPath + QVariantList{it.key()}, it.value());
            }
        }
        emit statusMessage(tr("Pending changes — remember to Save!"));
    };

    auto updateValueAt = [this, settlementPath](const QVariantList &path, const QJsonValue &value)
    {
        applyValueAtPath(settlementPath + path, value);
        emit statusMessage(tr("Pending changes — remember to Save!"));
    };

    auto *page = new QWidget(this);
    auto *mainLayout = new QVBoxLayout(page);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    
    auto *container = new QWidget(page);
    container->setFixedWidth(500);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    
    mainLayout->addWidget(container);
    mainLayout->addStretch();

    auto *customGroup = new QGroupBox(tr("Customization"), page);
    auto *customLayout = new QFormLayout(customGroup);
    customLayout->setHorizontalSpacing(12);
    customLayout->setVerticalSpacing(6);

    auto *nameField = new QLineEdit(customGroup);
    const QString nameKey = settlement.contains(kKeySettlementNameLong) ? kKeySettlementNameLong : kKeySettlementName;
    nameField->setText(stringForKeys(settlement, {kKeySettlementNameLong, kKeySettlementName}));
    customLayout->addRow(tr("Name"), nameField);
    connect(nameField, &QLineEdit::editingFinished, this, [nameField, updateSettlement, nameKey]()
            {
        const QString value = nameField->text();
        updateSettlement([value, nameKey](QJsonObject &obj) { obj.insert(nameKey, value); }); });

    auto *seedField = new QLineEdit(customGroup);
    auto seedTextFromValue = [](const QJsonValue &value) -> QString {
        if (value.isString())
        {
            return value.toString();
        }
        if (value.isDouble())
        {
            return QString::number(static_cast<qulonglong>(value.toDouble()));
        }
        if (value.isArray())
        {
            QJsonArray array = value.toArray();
            if (array.size() >= 2 && array.at(1).isString())
            {
                return array.at(1).toString();
            }
            if (!array.isEmpty() && array.at(0).isString())
            {
                return array.at(0).toString();
            }
        }
        return value.toVariant().toString();
    };

    QString seedText;
    if (settlement.contains(kKeySeedValueLong))
    {
        seedText = seedTextFromValue(settlement.value(kKeySeedValueLong));
    }
    else if (settlement.contains(kKeySeedValue))
    {
        seedText = seedTextFromValue(settlement.value(kKeySeedValue));
    }
    else
    {
        QJsonObject seedObj = settlement.value(kKeySettlementSeed).toObject();
        seedText = seedTextFromValue(seedObj.value(kKeySeedValue));
    }
    seedField->setText(seedText);
    customLayout->addRow(tr("Seed"), seedField);
    connect(seedField, &QLineEdit::editingFinished, this, [seedField, updateSettlement]()
            {
        QString raw = seedField->text().trimmed();
        if (raw.isEmpty()) {
            return;
        }
        bool ok = false;
        qulonglong seed = raw.startsWith("0x", Qt::CaseInsensitive)
                              ? raw.mid(2).toULongLong(&ok, 16)
                              : raw.toULongLong(&ok, 10);
        if (!ok) {
            return;
        }
        updateSettlement([seed, raw](QJsonObject &obj) {
            if (obj.contains(kKeySeedValueLong)) {
                QString formatted = raw.startsWith("0x", Qt::CaseInsensitive)
                                        ? raw
                                        : QString("0x%1").arg(seed, 0, 16).toUpper();
                obj.insert(kKeySeedValueLong, formatted);
                return;
            }
            if (obj.contains(kKeySeedValue)) {
                obj.insert(kKeySeedValue, static_cast<qint64>(seed));
                return;
            }
            QJsonObject seedObj = obj.value(kKeySettlementSeed).toObject();
            seedObj.insert(kKeySeedValue, static_cast<qint64>(seed));
            obj.insert(kKeySettlementSeed, seedObj);
        }); });

    layout->addWidget(customGroup);

    auto *adminGroup = new QGroupBox(tr("Settlement Administration Terminal"), page);
    auto *adminLayout = new QFormLayout(adminGroup);
    adminLayout->setHorizontalSpacing(12);
    adminLayout->setVerticalSpacing(6);

    auto readDecision = [this](const QJsonObject &obj, const QString &keyLong, const QString &keyShort) -> QString {
        QJsonValue value = obj.value(keyLong);
        if (value.isUndefined()) {
            value = obj.value(keyShort);
        }
        if (value.isObject()) {
            QJsonObject nested = value.toObject();
            return stringForKeys(nested, {kKeyJudgementTypeLong, kKeyJudgementTypeShort, kKeyLastDecisionLong, kKeyLastDecision});
        }
        return value.toString();
    };

    auto decisionDisplay = [](const QString &raw) -> QString {
        if (raw == "ProcPerkRelated") {
            return "Settler request pending";
        }
        if (raw == "BuildingChoice") {
            return "Construction pending";
        }
        if (raw == "StrangerVisit") {
            return "Visitor waiting";
        }
        if (raw == "Conflict") {
            return "Conflict resolution pending";
        }
        if (raw.isEmpty()) {
            return raw;
        }
        return raw;
    };

    auto addDecisionOptions = [decisionDisplay](QComboBox *combo) {
        struct DecisionItem { const char *raw; };
        const DecisionItem items[] = {
            {"None"},
            {"ProcPerkRelated"},
            {"BuildingChoice"},
            {"StrangerVisit"},
            {"Conflict"},
            {"PolicyDecision"},
            {"Dispute"},
            {"VisitorEvent"},
            {"CustomJudgement"},
        };
        for (const DecisionItem &item : items) {
            combo->addItem(decisionDisplay(item.raw), QString(item.raw));
        }
    };

    const QString pendingDecision = readDecision(settlement, kKeyPendingDecisionLong, kKeyPendingDecision);
    auto *pendingCombo = new QComboBox(adminGroup);
    pendingCombo->setEditable(true);
    addDecisionOptions(pendingCombo);
    if (!pendingDecision.isEmpty() && pendingCombo->findData(pendingDecision) < 0)
    {
        pendingCombo->addItem(decisionDisplay(pendingDecision), pendingDecision);
    }
    pendingCombo->setCurrentIndex(pendingCombo->findData(pendingDecision));
    adminLayout->addRow(tr("Pending Decision"), pendingCombo);
    connect(pendingCombo, &QComboBox::currentTextChanged, this, [pendingCombo, updateSettlement]()
            {
        QString value = pendingCombo->currentData().toString();
        if (value.isEmpty()) {
            value = pendingCombo->currentText();
        }
        updateSettlement([value](QJsonObject &obj) {
            const QString key = obj.contains(kKeyPendingDecisionLong) ? kKeyPendingDecisionLong : kKeyPendingDecision;
            QJsonObject pendingObj;
            pendingObj.insert(kKeyJudgementTypeLong, value);
            obj.insert(key, pendingObj);
        }); });

    const QString lastDecision = readDecision(settlement, kKeyLastDecisionLong, kKeyLastDecision);
    auto *lastCombo = new QComboBox(adminGroup);
    lastCombo->setEditable(true);
    addDecisionOptions(lastCombo);
    if (!lastDecision.isEmpty() && lastCombo->findData(lastDecision) < 0)
    {
        lastCombo->addItem(decisionDisplay(lastDecision), lastDecision);
    }
    lastCombo->setCurrentIndex(lastCombo->findData(lastDecision));
    adminLayout->addRow(tr("Last Decision"), lastCombo);
    connect(lastCombo, &QComboBox::currentTextChanged, this, [lastCombo, updateSettlement]()
            {
        QString value = lastCombo->currentData().toString();
        if (value.isEmpty()) {
            value = lastCombo->currentText();
        }
        updateSettlement([value](QJsonObject &obj) {
            const QString key = obj.contains(kKeyLastDecisionLong) ? kKeyLastDecisionLong : kKeyLastDecision;
            obj.insert(key, value);
        }); });

    auto *lastTimeField = new QDateTimeEdit(adminGroup);
    lastTimeField->setDisplayFormat("yyyy-MM-dd HH:mm:ss");
    lastTimeField->setCalendarPopup(true);
    QJsonValue lastTimeValue = settlement.value(kKeyLastDecisionTime);
    if (lastTimeValue.isUndefined())
    {
        lastTimeValue = settlement.value(kKeyLastDecisionTimeLong);
    }
    const QString lastTimeKey =
        settlement.contains(kKeyLastDecisionTimeLong) ? kKeyLastDecisionTimeLong : kKeyLastDecisionTime;
    const qint64 rawTime = lastTimeValue.toVariant().toLongLong();
    const bool isMs = rawTime > 100000000000LL;
    const qint64 normalized = isMs ? rawTime / 1000 : rawTime;
    lastTimeField->setDateTime(QDateTime::fromSecsSinceEpoch(normalized, Qt::LocalTime));
    adminLayout->addRow(tr("Last Decision Time"), lastTimeField);
    connect(lastTimeField, &QDateTimeEdit::dateTimeChanged, this, [lastTimeField, updateValueAt, lastTimeKey, isMs]()
            {
        qint64 value = lastTimeField->dateTime().toSecsSinceEpoch();
        if (isMs) {
            value *= 1000;
        }
        updateValueAt({lastTimeKey}, value); });

    layout->addWidget(adminGroup);

    auto *statsGroup = new QGroupBox(tr("Stats"), page);
    auto *statsLayout = new QFormLayout(statsGroup);
    statsLayout->setHorizontalSpacing(12);
    statsLayout->setVerticalSpacing(6);

    auto addNumericField = [statsGroup, statsLayout, updateValueAt](const QString &label, const QVariantList &path, qint64 value)
    {
        auto *field = new QLineEdit(statsGroup);
        field->setValidator(new QIntValidator(0, 9999999, field));
        field->setText(QString::number(value));
        statsLayout->addRow(label, field);
        QObject::connect(field, &QLineEdit::editingFinished, statsGroup, [field, updateValueAt, path]()
                {
            bool ok = false;
            qint64 updatedValue = field->text().toLongLong(&ok);
            if (!ok) {
                return;
            }
            updateValueAt(path, updatedValue); });
    };

    QJsonValue populationValue = settlement.value(kKeySettlementPopulation);
    if (populationValue.isUndefined())
    {
        populationValue = settlement.value(kKeySettlementPopulationLong);
    }
    const QString populationKey = settlement.contains(kKeySettlementPopulationLong) ? kKeySettlementPopulationLong : kKeySettlementPopulation;
    addNumericField(tr("Population"), {populationKey}, populationValue.toVariant().toLongLong());

    QJsonArray stats = settlement.value(kKeySettlementStatsLong).toArray();
    QString statsKey = kKeySettlementStatsLong;
    if (stats.isEmpty())
    {
        stats = settlement.value(kKeySettlementStatsShort).toArray();
        statsKey = kKeySettlementStatsShort;
    }
    if (stats.isEmpty())
    {
        stats = settlement.value(kKeySettlementStats).toArray();
        statsKey = kKeySettlementStats;
    }

    const bool statsAreObjects = !stats.isEmpty() && stats.at(0).isObject();
    if (statsAreObjects)
    {
        for (int i = 0; i < stats.size(); ++i)
        {
            QJsonObject stat = stats.at(i).toObject();
            if (stat.isEmpty())
            {
                continue;
            }
            QString statId = stringForKeys(stat, {kKeySettlementStatId, "BaseStatID"});
            QString label = statId;
            if (statId == "SETTLE_HAPP")
            {
                label = tr("Happiness");
            }
            else if (statId == "SETTLE_PROD")
            {
                label = tr("Productivity");
            }
            else if (statId == "SETTLE_MAINT")
            {
                label = tr("Maintenance Cost");
            }
            else if (statId == "SETTLE_DEBT")
            {
                label = tr("Debt");
            }

            addNumericField(label, {statsKey, i, kKeySettlementValue},
                            stat.value(kKeySettlementValue).toVariant().toLongLong());
        }
    }
    else
    {
        auto updateStatsIndex = [updateSettlement, statsKey](int index, qint64 value)
        {
            updateSettlement([statsKey, index, value](QJsonObject &obj) {
                QJsonArray stats = obj.value(statsKey).toArray();
                while (stats.size() <= index) {
                    stats.append(0);
                }
                stats[index] = value;
                obj.insert(statsKey, stats);
            });
        };

        auto addStatsField = [statsGroup, statsLayout, stats, updateStatsIndex](const QString &label, int index)
        {
            qint64 current = 0;
            if (index >= 0 && index < stats.size())
            {
                current = stats.at(index).toVariant().toLongLong();
            }
            auto *field = new QLineEdit(statsGroup);
            field->setValidator(new QIntValidator(0, 9999999, field));
            field->setText(QString::number(current));
            statsLayout->addRow(label, field);
            QObject::connect(field, &QLineEdit::editingFinished, statsGroup, [field, updateStatsIndex, index]()
                    {
                bool ok = false;
                qint64 updatedValue = field->text().toLongLong(&ok);
                if (!ok) {
                    return;
                }
                updateStatsIndex(index, updatedValue); });
        };

        addStatsField(tr("Happiness"), 1);
        addStatsField(tr("Productivity"), 2);
        addStatsField(tr("Maintenance Cost"), 3);
        addStatsField(tr("Sentinels"), 4);
        addStatsField(tr("Debt"), 5);
        addStatsField(tr("Sentinel Alert Level"), 6);
    }

    layout->addWidget(statsGroup);

    auto *perksGroup = new QGroupBox(tr("Perks"), page);
    auto *perksLayout = new QVBoxLayout(perksGroup);
    perksLayout->setContentsMargins(10, 10, 10, 10);
    perksLayout->setSpacing(6);

    QJsonArray perks = settlement.value(kKeySettlementPerksLong).toArray();
    QString perksKey = kKeySettlementPerksLong;
    if (perks.isEmpty())
    {
        perks = settlement.value(kKeySettlementPerks).toArray();
        perksKey = kKeySettlementPerks;
    }

    auto *perksList = new QListWidget(perksGroup);
    for (const QJsonValue &perk : perks)
    {
        const QString raw = perk.toString();
        QString display = ItemDefinitionRegistry::displayNameForId(raw);
        if (display.isEmpty())
        {
            display = raw;
        }
        auto *item = new QListWidgetItem(display, perksList);
        item->setData(Qt::UserRole, raw);
    }
    perksLayout->addWidget(perksList);

    auto *perksRow = new QHBoxLayout();
    auto *perkField = new QLineEdit(perksGroup);
    perkField->setPlaceholderText(tr("Add perk id..."));
    auto *addPerkButton = new QPushButton(tr("Add"), perksGroup);
    auto *removePerkButton = new QPushButton(tr("Remove Selected"), perksGroup);
    perksRow->addWidget(perkField);
    perksRow->addWidget(addPerkButton);
    perksRow->addWidget(removePerkButton);
    perksLayout->addLayout(perksRow);

    auto commitPerks = [perksList, updateSettlement, perksKey]()
    {
        QJsonArray updated;
        for (int i = 0; i < perksList->count(); ++i)
        {
            QListWidgetItem *item = perksList->item(i);
            QString raw = item->data(Qt::UserRole).toString();
            if (raw.isEmpty())
            {
                raw = item->text();
            }
            updated.append(raw);
        }
        updateSettlement([perksKey, updated](QJsonObject &obj) {
            obj.insert(perksKey, updated);
        });
    };

    connect(addPerkButton, &QPushButton::clicked, perksGroup, [perkField, perksList, commitPerks]()
            {
        const QString value = perkField->text().trimmed();
        if (value.isEmpty()) {
            return;
        }
        QString display = ItemDefinitionRegistry::displayNameForId(value);
        if (display.isEmpty())
        {
            display = value;
        }
        auto *item = new QListWidgetItem(display, perksList);
        item->setData(Qt::UserRole, value);
        perkField->clear();
        commitPerks(); });

    connect(removePerkButton, &QPushButton::clicked, perksGroup, [perksList, commitPerks]()
            {
        QList<QListWidgetItem *> selected = perksList->selectedItems();
        for (QListWidgetItem *item : selected)
        {
            delete item;
        }
        if (!selected.isEmpty())
        {
            commitPerks();
        } });

    layout->addWidget(perksGroup);
    layout->addStretch();
    return page;
}

void SettlementManagerPage::updateActiveContext()
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

QVariantList SettlementManagerPage::playerBasePath() const
{
    if (usingExpeditionContext_)
    {
        return {kKeyExpeditionContext, "6f="};
    }
    return {kKeyPlayerState, "6f="};
}

QVariantList SettlementManagerPage::settlementStatesPath() const
{
    return settlementStatesPath_;
}

void SettlementManagerPage::resolveSettlementStatesPath()
{
    settlementStatesPath_.clear();
    if (!rootDoc_.isObject())
    {
        return;
    }

    QJsonObject root = rootDoc_.object();
    if (root.value(kKeySettlementLocalData).isObject())
    {
        QVariantList rootPath = {kKeySettlementLocalData, kKeySettlementStates};
        if (valueAtPath(rootDoc_.object(), rootPath).isArray())
        {
            settlementStatesPath_ = rootPath;
            return;
        }
    }
    if (root.value(kKeySettlementLocalDataLong).isObject())
    {
        QVariantList rootPath = {kKeySettlementLocalDataLong, kKeySettlementStatesLong};
        if (valueAtPath(rootDoc_.object(), rootPath).isArray())
        {
            settlementStatesPath_ = rootPath;
            return;
        }
    }

    QVariantList playerPath = playerBasePath();
    QVariantList playerStatesPath = playerPath;
    playerStatesPath << kKeySettlementStates;
    if (valueAtPath(rootDoc_.object(), playerStatesPath).isArray())
    {
        settlementStatesPath_ = playerStatesPath;
        return;
    }
    QVariantList playerStatesPathLong = playerPath;
    playerStatesPathLong << kKeySettlementStatesLong;
    if (valueAtPath(rootDoc_.object(), playerStatesPathLong).isArray())
    {
        settlementStatesPath_ = playerStatesPathLong;
        return;
    }
    QVariantList playerNestedPath = playerPath;
    playerNestedPath << kKeySettlementLocalData << kKeySettlementStates;
    if (valueAtPath(rootDoc_.object(), playerNestedPath).isArray())
    {
        settlementStatesPath_ = playerNestedPath;
        return;
    }
    QVariantList playerNestedPathLong = playerPath;
    playerNestedPathLong << kKeySettlementLocalDataLong << kKeySettlementStatesLong;
    if (valueAtPath(rootDoc_.object(), playerNestedPathLong).isArray())
    {
        settlementStatesPath_ = playerNestedPathLong;
        return;
    }

    settlementStatesPath_ = findSettlementStatesPath(rootDoc_.object(), {});
}

QVariantList SettlementManagerPage::findSettlementStatesPath(const QJsonValue &value,
                                                            const QVariantList &path) const
{
    if (value.isObject())
    {
        QJsonObject obj = value.toObject();
        if (obj.contains(kKeySettlementStates) && obj.value(kKeySettlementStates).isArray())
        {
            QVariantList found = path;
            found << kKeySettlementStates;
            return found;
        }
        if (obj.contains(kKeySettlementStatesLong) && obj.value(kKeySettlementStatesLong).isArray())
        {
            QVariantList found = path;
            found << kKeySettlementStatesLong;
            return found;
        }
        for (auto it = obj.begin(); it != obj.end(); ++it)
        {
            QVariantList nextPath = path;
            nextPath << it.key();
            QVariantList found = findSettlementStatesPath(it.value(), nextPath);
            if (!found.isEmpty())
            {
                return found;
            }
        }
    }
    else if (value.isArray())
    {
        QJsonArray array = value.toArray();
        for (int i = 0; i < array.size(); ++i)
        {
            QVariantList nextPath = path;
            nextPath << i;
            QVariantList found = findSettlementStatesPath(array.at(i), nextPath);
            if (!found.isEmpty())
            {
                return found;
            }
        }
    }
    return {};
}

QList<SettlementManagerPage::SettlementEntry> SettlementManagerPage::collectOwnedSettlements() const
{
    QList<SettlementEntry> results;
    if (settlementStatesPath_.isEmpty())
    {
        return results;
    }

    QSet<QString> ownerLids;
    QSet<QString> ownerUids;
    QSet<QString> ownerUsns;
    collectPlayerOwnerIds(ownerLids, ownerUids, ownerUsns);
    const QString username = resolveUsername();

    QJsonArray states = valueAtPath(rootDoc_.object(), settlementStatesPath_).toArray();
    for (int i = 0; i < states.size(); ++i)
    {
        QJsonObject settlement = states.at(i).toObject();
        QJsonObject owner = objectForKeys(settlement, {kKeySettlementOwnerLong, kKeySettlementOwner});
        QString ownerName = stringForKeys(owner, {kKeyOwnerUsnLong, kKeyOwnerUsn, kKeyUsernameLong, kKeyUsername});
        QString ownerUid = stringForKeys(owner, {kKeyOwnerUidLong, kKeyOwnerUid});
        QString ownerLid = stringForKeys(owner, {kKeyOwnerLidLong, kKeyOwnerLid});

        bool matched = false;
        if (!ownerUsns.isEmpty() && !ownerName.isEmpty())
        {
            matched = ownerUsns.contains(ownerName);
        }
        if (!matched && !ownerUids.isEmpty() && !ownerUid.isEmpty())
        {
            matched = ownerUids.contains(ownerUid);
        }
        if (!matched && !ownerLids.isEmpty() && !ownerLid.isEmpty())
        {
            matched = ownerLids.contains(ownerLid);
        }
        if (!matched && !username.isEmpty() && !ownerName.isEmpty())
        {
            matched = (ownerName == username);
        }
        if (!matched)
        {
            continue;
        }
        SettlementEntry entry;
        entry.index = i;
        entry.name = stringForKeys(settlement, {kKeySettlementNameLong, kKeySettlementName});
        if (entry.name.isEmpty())
        {
            entry.name = tr("Settlement %1").arg(i);
        }
        results.append(entry);
    }
    return results;
}

void SettlementManagerPage::collectPlayerOwnerIds(QSet<QString> &lids, QSet<QString> &uids, QSet<QString> &usns) const
{
    auto addOwner = [&lids, &uids, &usns, this](const QJsonObject &owner)
    {
        if (owner.isEmpty())
        {
            return;
        }
        QString lid = stringForKeys(owner, {kKeyOwnerLidLong, kKeyOwnerLid});
        QString uid = stringForKeys(owner, {kKeyOwnerUidLong, kKeyOwnerUid});
        QString usn = stringForKeys(owner, {kKeyOwnerUsnLong, kKeyOwnerUsn});
        if (!lid.isEmpty())
        {
            lids.insert(lid);
        }
        if (!uid.isEmpty())
        {
            uids.insert(uid);
        }
        if (!usn.isEmpty())
        {
            usns.insert(usn);
        }
    };

    QJsonObject root = rootDoc_.object();
    QJsonObject commonState = objectForKeys(root, {kKeyCommonStateLong, kKeyCommonState});
    QJsonArray usedOwners = commonState.value(kKeyUsedDiscoveryOwners).toArray();
    if (usedOwners.isEmpty())
    {
        usedOwners = commonState.value(kKeyUsedDiscoveryOwnersLong).toArray();
    }
    for (const QJsonValue &value : usedOwners)
    {
        addOwner(value.toObject());
    }

    QJsonObject discoveryManager = objectForKeys(root, {kKeyDiscoveryManagerLong, kKeyDiscoveryManager});
    QJsonObject discoveryData = objectForKeys(discoveryManager, {kKeyDiscoveryDataLong, kKeyDiscoveryData});
    QJsonArray bases = discoveryData.value(kKeyPersistentBases).toArray();
    if (bases.isEmpty())
    {
        bases = discoveryData.value(kKeyPersistentBasesLong).toArray();
    }
    for (const QJsonValue &value : bases)
    {
        QJsonObject base = value.toObject();
        QJsonObject owner = objectForKeys(base, {kKeySettlementOwnerLong, kKeySettlementOwner});
        addOwner(owner);
    }
}

QString SettlementManagerPage::stringForKeys(const QJsonObject &obj, const QStringList &keys) const
{
    for (const QString &key : keys)
    {
        if (obj.contains(key))
        {
            return obj.value(key).toString();
        }
    }
    return QString();
}

QJsonObject SettlementManagerPage::objectForKeys(const QJsonObject &obj, const QStringList &keys) const
{
    for (const QString &key : keys)
    {
        if (obj.contains(key) && obj.value(key).isObject())
        {
            return obj.value(key).toObject();
        }
    }
    return {};
}

QString SettlementManagerPage::resolveUsername() const
{
    QVariantList basePath = playerBasePath();
    QVariantList usernamePath = basePath;
    usernamePath << kKeyUsername;
    QString username = valueAtPath(rootDoc_.object(), usernamePath).toString();
    if (!username.isEmpty())
    {
        return username;
    }
    QVariantList usernamePathLong = basePath;
    usernamePathLong << kKeyUsernameLong;
    username = valueAtPath(rootDoc_.object(), usernamePathLong).toString();
    if (!username.isEmpty())
    {
        return username;
    }

    auto search = [](const QJsonValue &value, const QString &key, auto &&searchRef) -> QString
    {
        if (value.isObject())
        {
            QJsonObject obj = value.toObject();
            if (obj.contains(key))
            {
                return obj.value(key).toString();
            }
            for (auto it = obj.begin(); it != obj.end(); ++it)
            {
                QString found = searchRef(it.value(), key, searchRef);
                if (!found.isEmpty())
                {
                    return found;
                }
            }
        }
        else if (value.isArray())
        {
            QJsonArray array = value.toArray();
            for (const QJsonValue &entry : array)
            {
                QString found = searchRef(entry, key, searchRef);
                if (!found.isEmpty())
                {
                    return found;
                }
            }
        }
        return QString();
    };

    QString found = search(rootDoc_.object(), kKeyUsername, search);
    if (!found.isEmpty())
    {
        return found;
    }
    return search(rootDoc_.object(), kKeyUsernameLong, search);
}

QJsonObject SettlementManagerPage::settlementAtIndex(int index) const
{
    if (index < 0)
    {
        return {};
    }
    QJsonArray states = valueAtPath(rootDoc_.object(), settlementStatesPath_).toArray();
    if (index >= states.size())
    {
        return {};
    }
    return states.at(index).toObject();
}

QJsonValue SettlementManagerPage::valueAtPath(const QJsonValue &root, const QVariantList &path) const
{
    QJsonValue current = root;
    for (const QVariant &segment : path)
    {
        if (segment.canConvert<int>() && current.isArray())
        {
            int index = segment.toInt();
            QJsonArray array = current.toArray();
            if (index < 0 || index >= array.size())
            {
                return QJsonValue();
            }
            current = array.at(index);
        }
        else if (segment.canConvert<QString>() && current.isObject())
        {
            QJsonObject obj = current.toObject();
            QString key = segment.toString();
            current = obj.value(key);
        }
        else
        {
            return QJsonValue();
        }
    }
    return current;
}

QJsonValue SettlementManagerPage::setValueAtPath(const QJsonValue &root, const QVariantList &path,
                                                 int depth, const QJsonValue &value) const
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

void SettlementManagerPage::applyValueAtPath(const QVariantList &path, const QJsonValue &value)
{
    if (valueAtPath(rootDoc_.object(), path) == value) {
        return;
    }
    QJsonValue updated = setValueAtPath(rootDoc_.object(), path, 0, value);
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
