#pragma once

#include <QHash>
#include <QList>
#include <QString>

enum class ItemType {
    Substance,
    Product,
    Technology,
    Unknown
};

struct ItemEntry {
    QString id;
    QString displayName;
    ItemType type = ItemType::Unknown;
    int maxStack = 0;
};

class ItemCatalog
{
public:
    static QList<ItemEntry> itemsForTypes(const QList<ItemType> &allowedTypes);
    static void warmup();

private:
    static void ensureLoaded();
    static void loadCatalog();
    static void parseProductTable(QHash<QString, ItemEntry> &entries);
    static void parseBasePartProductTable(QHash<QString, ItemEntry> &entries);
    static void parseSubstanceTable(QHash<QString, ItemEntry> &entries);
    static void parseTechnologyTable(QHash<QString, ItemEntry> &entries);
    static void parseProceduralTechnologyTable(QHash<QString, ItemEntry> &entries);

    static int readIntAttribute(const QString &value, int fallback);
    static QString normalizeId(const QString &value);
};
