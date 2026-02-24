#include "ui/MaterialLookupDialog.h"

#include "core/ResourceLocator.h"
#include "registry/IconRegistry.h"
#include "registry/ItemCatalog.h"
#include "registry/ItemDefinitionRegistry.h"
#include "registry/LocalizationRegistry.h"

#include <algorithm>
#include <QDialog>
#include <QDomDocument>
#include <QFile>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QIcon>
#include <QPixmap>
#include <QRegularExpression>
#include <QShowEvent>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

namespace
{
QString normalizeId(const QString &value)
{
    QString id = value.trimmed();
    if (id.startsWith('^')) {
        id = id.mid(1);
    }
    const int hashIndex = id.indexOf('#');
    if (hashIndex >= 0) {
        id = id.left(hashIndex);
    }
    return id.toUpper();
}

QString humanizeCategory(const QString &value)
{
    if (value.isEmpty()) {
        return QString();
    }
    QString out;
    out.reserve(value.size() + 4);
    QChar prev;
    for (int i = 0; i < value.size(); ++i) {
        const QChar ch = value.at(i);
        if (ch == '_' || ch == '-') {
            out.append(' ');
            prev = ch;
            continue;
        }
        if (i > 0 && ch.isUpper() && prev.isLower()) {
            out.append(' ');
        } else if (i > 0 && ch.isDigit() && !prev.isDigit()) {
            out.append(' ');
        }
        out.append(ch);
        prev = ch;
    }
    return out.trimmed();
}

QString itemTypeLabel(ItemType type)
{
    switch (type) {
    case ItemType::Substance:
        return QObject::tr("Substance");
    case ItemType::Product:
        return QObject::tr("Product");
    case ItemType::Technology:
        return QObject::tr("Technology");
    default:
        return QObject::tr("Unknown");
    }
}

QDomElement findProperty(const QDomElement &parent, const QString &name)
{
    QDomElement child = parent.firstChildElement("Property");
    while (!child.isNull()) {
        if (child.attribute("name") == name) {
            return child;
        }
        child = child.nextSiblingElement("Property");
    }
    return {};
}

QString propertyValue(const QDomElement &parent, const QString &name)
{
    return findProperty(parent, name).attribute("value");
}

QString nestedPropertyValue(const QDomElement &parent, const QString &name)
{
    QDomElement holder = findProperty(parent, name);
    if (holder.isNull()) {
        return QString();
    }
    QDomElement nested = holder.firstChildElement("Property");
    while (!nested.isNull()) {
        const QString nestedValue = nested.attribute("value");
        if (!nestedValue.isEmpty()) {
            return nestedValue;
        }
        nested = nested.nextSiblingElement("Property");
    }
    return holder.attribute("value");
}

int readIntValue(const QString &value, int fallback = 0)
{
    if (value.isEmpty()) {
        return fallback;
    }
    bool ok = false;
    const int number = qRound(value.toDouble(&ok));
    return ok ? number : fallback;
}

QString resolveTextToken(const QString &token)
{
    if (token.isEmpty()) {
        return QString();
    }
    QString resolved = LocalizationRegistry::resolveToken(token);
    if (resolved.isEmpty()) {
        resolved = token;
    }
    resolved.replace("&#xA;", "\n");
    resolved.replace(QRegularExpression("<[^>]*>"), "");
    return resolved.trimmed();
}

struct Requirement
{
    QString id;
    QString type;
    int amount = 0;
};

struct MaterialRecord
{
    QString id;
    ItemType type = ItemType::Unknown;
    QString category;
    QString nameToken;
    QString subtitleToken;
    QString descriptionToken;
    int chargeAmount = 0;
    int maxStack = 0;
    QList<Requirement> requirements;
};

struct UsageEntry
{
    QString id;
    ItemType type = ItemType::Unknown;
    QString category;
    int amount = 0;
};

QHash<QString, MaterialRecord> g_records;
QHash<QString, QList<UsageEntry>> g_usageByIngredient;
bool g_materialDataLoaded = false;

QList<Requirement> parseRequirements(const QDomElement &itemElement)
{
    QList<Requirement> out;
    QDomElement requirements = findProperty(itemElement, "Requirements");
    if (requirements.isNull()) {
        return out;
    }

    QDomElement requirement = requirements.firstChildElement("Property");
    while (!requirement.isNull()) {
        if (requirement.attribute("name") == "Requirements") {
            Requirement req;
            req.id = normalizeId(requirement.attribute("_id"));
            if (req.id.isEmpty()) {
                req.id = normalizeId(propertyValue(requirement, "ID"));
            }
            req.type = nestedPropertyValue(requirement, "Type");
            req.amount = readIntValue(propertyValue(requirement, "Amount"), 0);
            if (!req.id.isEmpty()) {
                out.append(req);
            }
        }
        requirement = requirement.nextSiblingElement("Property");
    }
    return out;
}

void parseTableFile(const QString &path,
                    const QString &tableValue,
                    ItemType type,
                    const QString &categoryPath,
                    const QString &chargeProp,
                    QHash<QString, MaterialRecord> &records,
                    QHash<QString, QList<UsageEntry>> &usageByIngredient)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    QDomDocument doc;
    if (!doc.setContent(&file)) {
        return;
    }

    QDomNodeList nodes = doc.elementsByTagName("Property");
    for (int i = 0; i < nodes.count(); ++i) {
        QDomElement element = nodes.at(i).toElement();
        if (element.isNull() || element.attribute("value") != tableValue) {
            continue;
        }

        MaterialRecord record;
        record.id = normalizeId(element.attribute("_id"));
        if (record.id.isEmpty()) {
            record.id = normalizeId(propertyValue(element, "ID"));
        }
        if (record.id.isEmpty()) {
            continue;
        }
        record.type = type;
        record.nameToken = propertyValue(element, "Name");
        record.subtitleToken = propertyValue(element, "Subtitle");
        record.descriptionToken = propertyValue(element, "Description");
        record.chargeAmount = readIntValue(propertyValue(element, chargeProp), 0);
        record.requirements = parseRequirements(element);

        if (categoryPath.contains('/')) {
            const QStringList parts = categoryPath.split('/');
            QDomElement parent = findProperty(element, parts.first());
            if (!parent.isNull()) {
                QDomElement child = findProperty(parent, parts.last());
                record.category = child.attribute("value");
            }
        } else {
            record.category = nestedPropertyValue(element, categoryPath);
        }
        record.category = humanizeCategory(record.category);
        records.insert(record.id, record);

        for (const Requirement &req : record.requirements) {
            UsageEntry usage;
            usage.id = record.id;
            usage.type = record.type;
            usage.category = record.category;
            usage.amount = req.amount;
            usageByIngredient[req.id].append(usage);
        }
    }
}

void ensureMaterialDataLoaded(QHash<QString, MaterialRecord> &records,
                              QHash<QString, QList<UsageEntry>> &usageByIngredient)
{
    if (g_materialDataLoaded) {
        return;
    }

    parseTableFile(ResourceLocator::resolveResource("data/NMS_REALITY_GCPRODUCTTABLE.MXML"),
                   "GcProductData",
                   ItemType::Product,
                   "Type/ProductCategory",
                   "ChargeValue",
                   records,
                   usageByIngredient);
    parseTableFile(ResourceLocator::resolveResource("data/NMS_BASEPARTPRODUCTS.MXML"),
                   "GcProductData",
                   ItemType::Product,
                   "Type/ProductCategory",
                   "ChargeValue",
                   records,
                   usageByIngredient);
    parseTableFile(ResourceLocator::resolveResource("data/NMS_REALITY_GCSUBSTANCETABLE.MXML"),
                   "GcRealitySubstanceData",
                   ItemType::Substance,
                   "Category/SubstanceCategory",
                   "ChargeValue",
                   records,
                   usageByIngredient);
    parseTableFile(ResourceLocator::resolveResource("data/NMS_REALITY_GCTECHNOLOGYTABLE.MXML"),
                   "GcTechnology",
                   ItemType::Technology,
                   "Category/TechnologyCategory",
                   "ChargeAmount",
                   records,
                   usageByIngredient);

    const QList<ItemEntry> all = ItemCatalog::itemsForTypes(
        {ItemType::Product, ItemType::Substance, ItemType::Technology});
    for (const ItemEntry &entry : all) {
        const QString normalized = normalizeId(entry.id);
        auto it = records.find(normalized);
        if (it != records.end()) {
            it->maxStack = entry.maxStack;
        }
    }
    g_materialDataLoaded = true;
}

MaterialRecord recordForId(const QString &id)
{
    ensureMaterialDataLoaded(g_records, g_usageByIngredient);
    return g_records.value(normalizeId(id));
}

QList<UsageEntry> usageForId(const QString &id)
{
    ensureMaterialDataLoaded(g_records, g_usageByIngredient);
    QList<UsageEntry> out = g_usageByIngredient.value(normalizeId(id));
    std::sort(out.begin(), out.end(), [](const UsageEntry &a, const UsageEntry &b) {
        return a.id.toLower() < b.id.toLower();
    });
    return out;
}

QLineEdit *makeReadOnlyField(const QString &text, QWidget *parent)
{
    auto *field = new QLineEdit(parent);
    field->setReadOnly(true);
    field->setText(text);
    return field;
}
}

MaterialLookupDialog::MaterialLookupDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Material Lookup"));
    setMinimumSize(420, 520);

    auto *layout = new QVBoxLayout(this);
    searchField_ = new QLineEdit(this);
    searchField_->setPlaceholderText(tr("Search by name or ID..."));
    listWidget_ = new QListWidget(this);

    layout->addWidget(searchField_);
    layout->addWidget(listWidget_);

    populateList();

    iconTimer_ = new QTimer(this);
    iconTimer_->setInterval(1);
    connect(iconTimer_, &QTimer::timeout, this, &MaterialLookupDialog::loadNextIconBatch);

    connect(searchField_, &QLineEdit::textChanged, this, &MaterialLookupDialog::filterList);
    connect(listWidget_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        QString id = item->data(Qt::UserRole).toString();
        QString name = item->text();
        showDetail(id, name);
    });
}

void MaterialLookupDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    startIconLoading();
}

void MaterialLookupDialog::populateList()
{
    listWidget_->clear();
    QHash<QString, ItemDefinition> defs = ItemDefinitionRegistry::allDefinitions();
    QList<QString> keys = defs.keys();
    std::sort(keys.begin(), keys.end(), [&defs](const QString &a, const QString &b) {
        return defs.value(a).name.toLower() < defs.value(b).name.toLower();
    });
    for (const QString &key : keys) {
        ItemDefinition def = defs.value(key);
        QString label = def.name.isEmpty() ? key : QString("%1 (%2)").arg(def.name, key);
        auto *item = new QListWidgetItem(label, listWidget_);
        item->setData(Qt::UserRole, key);
    }
}

void MaterialLookupDialog::filterList(const QString &text)
{
    QString needle = text.trimmed().toLower();
    for (int i = 0; i < listWidget_->count(); ++i) {
        QListWidgetItem *item = listWidget_->item(i);
        bool match = needle.isEmpty() || item->text().toLower().contains(needle);
        item->setHidden(!match);
    }
}

void MaterialLookupDialog::showDetail(const QString &id, const QString &name)
{
    const QString normalized = normalizeId(id);
    ItemDefinition definition = ItemDefinitionRegistry::definitionForId(normalized);
    MaterialRecord record = recordForId(normalized);
    QList<UsageEntry> usage = usageForId(normalized);

    QString displayName = resolveTextToken(record.nameToken);
    if (displayName.isEmpty()) {
        displayName = definition.name.isEmpty() ? name : definition.name;
    }
    QString subtitle = resolveTextToken(record.subtitleToken);
    QString description = resolveTextToken(record.descriptionToken);
    QString category = record.category.isEmpty() ? tr("Unknown") : record.category;
    const QString type = itemTypeLabel(record.type);

    QDialog dialog(this);
    dialog.setWindowTitle(displayName);
    dialog.setMinimumSize(900, 520);

    auto *layout = new QVBoxLayout(&dialog);
    auto *topRow = new QHBoxLayout();
    auto *leftPanel = new QVBoxLayout();
    auto *rightPanel = new QVBoxLayout();

    auto *form = new QGridLayout();
    form->addWidget(new QLabel(tr("Type"), &dialog), 0, 0);
    form->addWidget(makeReadOnlyField(type, &dialog), 0, 1);
    form->addWidget(new QLabel(tr("Category"), &dialog), 1, 0);
    form->addWidget(makeReadOnlyField(category, &dialog), 1, 1);
    form->addWidget(new QLabel(tr("Name"), &dialog), 2, 0);
    form->addWidget(makeReadOnlyField(displayName, &dialog), 2, 1);
    form->addWidget(new QLabel(tr("Subtitle"), &dialog), 3, 0);
    form->addWidget(makeReadOnlyField(subtitle, &dialog), 3, 1);
    leftPanel->addLayout(form);

    auto *descLabel = new QLabel(tr("Description"), &dialog);
    auto *descriptionBox = new QTextEdit(&dialog);
    descriptionBox->setReadOnly(true);
    descriptionBox->setPlainText(description);
    descriptionBox->setMinimumHeight(120);
    leftPanel->addWidget(descLabel);
    leftPanel->addWidget(descriptionBox);

    auto *idRow = new QHBoxLayout();
    idRow->addWidget(new QLabel(tr("Id"), &dialog));
    idRow->addWidget(makeReadOnlyField(normalized, &dialog));
    rightPanel->addLayout(idRow);

    auto *iconLabel = new QLabel(&dialog);
    iconLabel->setMinimumSize(140, 140);
    iconLabel->setAlignment(Qt::AlignCenter);
    QPixmap icon = IconRegistry::iconForId(normalized);
    if (!icon.isNull()) {
        iconLabel->setPixmap(icon.scaled(140, 140, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    rightPanel->addWidget(iconLabel, 0, Qt::AlignTop);
    rightPanel->addStretch();

    topRow->addLayout(leftPanel, 1);
    topRow->addLayout(rightPanel);
    layout->addLayout(topRow);

    auto *bottomRow = new QHBoxLayout();

    auto *acquisitionGroup = new QGroupBox(tr("Acquisition"), &dialog);
    auto *acquisitionLayout = new QVBoxLayout(acquisitionGroup);
    auto *requirementsTable = new QTableWidget(acquisitionGroup);
    requirementsTable->setColumnCount(3);
    requirementsTable->setHorizontalHeaderLabels({tr("Name"), tr("Type"), tr("Amount")});
    requirementsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    requirementsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    requirementsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    requirementsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    requirementsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    requirementsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    requirementsTable->setRowCount(record.requirements.size());
    for (int row = 0; row < record.requirements.size(); ++row) {
        const Requirement &req = record.requirements.at(row);
        QString reqName = ItemDefinitionRegistry::displayNameForId(req.id);
        if (reqName.isEmpty()) {
            reqName = req.id;
        }
        auto *nameItem = new QTableWidgetItem(reqName);
        nameItem->setData(Qt::UserRole, req.id);
        requirementsTable->setItem(row, 0, nameItem);
        requirementsTable->setItem(row, 1, new QTableWidgetItem(humanizeCategory(req.type)));
        requirementsTable->setItem(row, 2, new QTableWidgetItem(QString::number(req.amount)));
    }
    connect(requirementsTable, &QTableWidget::itemDoubleClicked, &dialog, [this, requirementsTable](QTableWidgetItem *clicked) {
        if (!clicked) {
            return;
        }
        QTableWidgetItem *nameItem = requirementsTable->item(clicked->row(), 0);
        if (!nameItem) {
            return;
        }
        QString targetId = nameItem->data(Qt::UserRole).toString();
        if (targetId.isEmpty()) {
            targetId = nameItem->text();
        }
        if (targetId.isEmpty()) {
            return;
        }
        showDetail(targetId, nameItem->text());
    });
    acquisitionLayout->addWidget(requirementsTable);

    auto *usageGroup = new QGroupBox(tr("Usage"), &dialog);
    auto *usageLayout = new QVBoxLayout(usageGroup);
    auto *usageTable = new QTableWidget(usageGroup);
    usageTable->setColumnCount(4);
    usageTable->setHorizontalHeaderLabels({tr("Name"), tr("Type"), tr("Category"), tr("Amount")});
    usageTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    usageTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    usageTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    usageTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    usageTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    usageTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    usageTable->setSelectionMode(QAbstractItemView::SingleSelection);
    usageTable->setRowCount(usage.size());
    for (int row = 0; row < usage.size(); ++row) {
        const UsageEntry &entry = usage.at(row);
        QString usageName = ItemDefinitionRegistry::displayNameForId(entry.id);
        if (usageName.isEmpty()) {
            usageName = entry.id;
        }
        auto *nameItem = new QTableWidgetItem(usageName);
        nameItem->setData(Qt::UserRole, entry.id);
        usageTable->setItem(row, 0, nameItem);
        usageTable->setItem(row, 1, new QTableWidgetItem(itemTypeLabel(entry.type)));
        usageTable->setItem(row, 2, new QTableWidgetItem(entry.category));
        usageTable->setItem(row, 3, new QTableWidgetItem(QString::number(entry.amount)));
    }
    connect(usageTable, &QTableWidget::itemDoubleClicked, &dialog, [this, usageTable](QTableWidgetItem *clicked) {
        if (!clicked) {
            return;
        }
        QTableWidgetItem *nameItem = usageTable->item(clicked->row(), 0);
        if (!nameItem) {
            return;
        }
        QString targetId = nameItem->data(Qt::UserRole).toString();
        if (targetId.isEmpty()) {
            targetId = nameItem->text();
        }
        if (targetId.isEmpty()) {
            return;
        }
        showDetail(targetId, nameItem->text());
    });
    usageLayout->addWidget(usageTable);

    bottomRow->addWidget(acquisitionGroup, 1);
    bottomRow->addWidget(usageGroup, 2);
    layout->addLayout(bottomRow, 1);

    dialog.exec();
}

void MaterialLookupDialog::startIconLoading()
{
    if (!iconTimer_ || iconTimer_->isActive()) {
        return;
    }
    iconLoadIndex_ = 0;
    iconTimer_->start();
}

void MaterialLookupDialog::loadNextIconBatch()
{
    if (!listWidget_) {
        iconTimer_->stop();
        return;
    }

    const int count = listWidget_->count();
    const int batchSize = 40;
    int end = qMin(iconLoadIndex_ + batchSize, count);

    for (int i = iconLoadIndex_; i < end; ++i) {
        QListWidgetItem *item = listWidget_->item(i);
        if (!item) {
            continue;
        }
        QString id = item->data(Qt::UserRole).toString();
        if (id.isEmpty()) {
            continue;
        }
        QPixmap icon = IconRegistry::iconForId(id);
        if (!icon.isNull()) {
            item->setIcon(QIcon(icon));
        }
    }

    iconLoadIndex_ = end;
    if (iconLoadIndex_ >= count) {
        iconTimer_->stop();
    }
}
