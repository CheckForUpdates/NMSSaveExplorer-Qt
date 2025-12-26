#pragma once

#include <QHash>
#include <QString>

struct ItemDefinition
{
    QString name;
    QString icon;
};

class ItemDefinitionRegistry
{
public:
    static ItemDefinition definitionForId(const QString &itemId);
    static QString displayNameForId(const QString &itemId);
    static QHash<QString, ItemDefinition> allDefinitions();
    static bool isLoaded();

private:
    static void ensureLoaded();
    static void loadDefinitions();
    static QString normalizeKey(const QString &itemId);
    static QString fallbackKey(const QString &key);
};
