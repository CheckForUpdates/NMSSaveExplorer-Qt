#include "registry/ItemCatalog.h"

#include "core/ResourceLocator.h"
#include "registry/ItemDefinitionRegistry.h"

#include <QDomDocument>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>

namespace {
const char *kProductTable = "data/nms_reality_gcproducttable.MXML";
const char *kSubstanceTable = "data/nms_reality_gcsubstancetable.MXML";
const char *kTechnologyTable = "data/nms_reality_gctechnologytable.MXML";
const char *kCatalogCache = "item_catalog.json";

const QHash<ItemType, int> kBaseStacks = {
    {ItemType::Product, 10},
    {ItemType::Substance, 9999}
};

QList<ItemEntry> g_items;
bool g_loaded = false;
QMutex g_loadMutex;

ItemType itemTypeFromString(const QString &value)
{
    QString lower = value.trimmed().toLower();
    if (lower == "substance") {
        return ItemType::Substance;
    }
    if (lower == "product") {
        return ItemType::Product;
    }
    if (lower == "technology") {
        return ItemType::Technology;
    }
    return ItemType::Unknown;
}

bool loadCatalogCache()
{
    QString path = ResourceLocator::resolveResource(kCatalogCache);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isArray()) {
        return false;
    }

    QJsonArray array = doc.array();
    QList<ItemEntry> items;
    items.reserve(array.size());
    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            continue;
        }
        QJsonObject obj = value.toObject();
        QString id = obj.value("id").toString();
        if (id.isEmpty()) {
            continue;
        }
        ItemEntry entry;
        entry.id = id;
        entry.displayName = obj.value("displayName").toString();
        entry.type = itemTypeFromString(obj.value("type").toString());
        entry.maxStack = obj.value("maxStack").toInt(0);
        if (entry.displayName.isEmpty()) {
            entry.displayName = entry.id;
        }
        items.append(entry);
    }
    if (items.isEmpty()) {
        return false;
    }
    std::sort(items.begin(), items.end(), [](const ItemEntry &a, const ItemEntry &b) {
        return a.displayName.toLower() < b.displayName.toLower();
    });
    g_items = items;
    return true;
}
}

QList<ItemEntry> ItemCatalog::itemsForTypes(const QList<ItemType> &allowedTypes)
{
    ensureLoaded();
    if (allowedTypes.isEmpty()) {
        return g_items;
    }
    QList<ItemEntry> matches;
    for (const ItemEntry &entry : g_items) {
        if (allowedTypes.contains(entry.type)) {
            matches.append(entry);
        }
    }
    return matches;
}

void ItemCatalog::warmup()
{
    ensureLoaded();
}

void ItemCatalog::ensureLoaded()
{
    QMutexLocker locker(&g_loadMutex);
    if (g_loaded) {
        return;
    }
    loadCatalog();
    g_loaded = true;
}

void ItemCatalog::loadCatalog()
{
    if (loadCatalogCache()) {
        return;
    }
    QHash<QString, ItemEntry> entries;
    parseProductTable(entries);
    parseSubstanceTable(entries);
    parseTechnologyTable(entries);

    QHash<QString, ItemDefinition> definitions = ItemDefinitionRegistry::allDefinitions();
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        QString id = it.key();
        ItemEntry entry = it.value();
        ItemDefinition def = definitions.value(id);
        if (!def.name.isEmpty()) {
            entry.displayName = def.name;
        } else {
            entry.displayName = id;
        }
        g_items.append(entry);
    }
    std::sort(g_items.begin(), g_items.end(), [](const ItemEntry &a, const ItemEntry &b) {
        return a.displayName.toLower() < b.displayName.toLower();
    });
}

void ItemCatalog::parseProductTable(QHash<QString, ItemEntry> &entries)
{
    QString path = ResourceLocator::resolveResource(kProductTable);
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
        if (element.isNull()) {
            continue;
        }
        if (element.attribute("value") != "GcProductData") {
            continue;
        }
        QString id = normalizeId(element.attribute("_id"));
        if (id.isEmpty()) {
            continue;
        }
        QDomElement child = element.firstChildElement("Property");
        int multiplier = 1;
        while (!child.isNull()) {
            if (child.attribute("name") == "StackMultiplier") {
                multiplier = readIntAttribute(child.attribute("value"), 1);
                break;
            }
            child = child.nextSiblingElement("Property");
        }
        int base = kBaseStacks.value(ItemType::Product, 1);
        ItemEntry entry{ id, QString(), ItemType::Product, multiplier * base };
        entries.insert(id, entry);
    }
}

void ItemCatalog::parseSubstanceTable(QHash<QString, ItemEntry> &entries)
{
    QString path = ResourceLocator::resolveResource(kSubstanceTable);
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
        if (element.isNull()) {
            continue;
        }
        if (element.attribute("value") != "GcRealitySubstanceData") {
            continue;
        }
        QString id = normalizeId(element.attribute("_id"));
        if (id.isEmpty()) {
            continue;
        }
        QDomElement child = element.firstChildElement("Property");
        int multiplier = 1;
        while (!child.isNull()) {
            if (child.attribute("name") == "StackMultiplier") {
                multiplier = readIntAttribute(child.attribute("value"), 1);
                break;
            }
            child = child.nextSiblingElement("Property");
        }
        int base = kBaseStacks.value(ItemType::Substance, 1);
        ItemEntry entry{ id, QString(), ItemType::Substance, multiplier * base };
        entries.insert(id, entry);
    }
}

void ItemCatalog::parseTechnologyTable(QHash<QString, ItemEntry> &entries)
{
    QString path = ResourceLocator::resolveResource(kTechnologyTable);
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
        if (element.isNull()) {
            continue;
        }
        if (element.attribute("value") != "GcTechnology") {
            continue;
        }
        QString id = normalizeId(element.attribute("_id"));
        if (id.isEmpty()) {
            continue;
        }
        QDomElement child = element.firstChildElement("Property");
        int charge = 1;
        while (!child.isNull()) {
            if (child.attribute("name") == "ChargeAmount") {
                charge = readIntAttribute(child.attribute("value"), 1);
                if (charge <= 0) {
                    charge = 1;
                }
                break;
            }
            child = child.nextSiblingElement("Property");
        }
        ItemEntry entry{ id, QString(), ItemType::Technology, charge };
        entries.insert(id, entry);
    }
}

int ItemCatalog::readIntAttribute(const QString &value, int fallback)
{
    if (value.isEmpty()) {
        return fallback;
    }
    bool ok = false;
    double parsed = value.toDouble(&ok);
    if (!ok) {
        return fallback;
    }
    return static_cast<int>(qRound(parsed));
}

QString ItemCatalog::normalizeId(const QString &value)
{
    return value.trimmed().toUpper();
}
