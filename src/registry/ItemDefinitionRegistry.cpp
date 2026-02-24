#include "registry/ItemDefinitionRegistry.h"

#include "core/ResourceLocator.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
const char *kDefinitionPath = "localization_map.json";
QHash<QString, ItemDefinition> g_definitions;
bool g_loaded = false;
}

ItemDefinition ItemDefinitionRegistry::definitionForId(const QString &itemId)
{
    if (itemId.isEmpty()) {
        return {};
    }
    ensureLoaded();
    QString key = normalizeKey(itemId);
    ItemDefinition def = g_definitions.value(key);
    if (!def.name.isEmpty() || !def.icon.isEmpty()) {
        return def;
    }
    QString fallback = fallbackKey(key);
    if (!fallback.isEmpty()) {
        return g_definitions.value(fallback);
    }
    return {};
}

QString ItemDefinitionRegistry::displayNameForId(const QString &itemId)
{
    ItemDefinition def = definitionForId(itemId);
    return def.name;
}

QHash<QString, ItemDefinition> ItemDefinitionRegistry::allDefinitions()
{
    ensureLoaded();
    return g_definitions;
}

bool ItemDefinitionRegistry::isLoaded()
{
    return g_loaded;
}

void ItemDefinitionRegistry::ensureLoaded()
{
    if (g_loaded) {
        return;
    }
    loadDefinitions();
    g_loaded = true;
}

void ItemDefinitionRegistry::loadDefinitions()
{
    QString path = ResourceLocator::resolveResource(kDefinitionPath);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return;
    }

    QJsonObject root = doc.object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        if (!it.value().isObject()) {
            continue;
        }
        QJsonObject obj = it.value().toObject();
        QString name = obj.value("name").toString();
        QString icon = obj.value("icon").toString();
        if (name.isEmpty() && icon.isEmpty()) {
            continue;
        }
        QString key = it.key().toUpper();
        g_definitions.insert(key, ItemDefinition{name, icon});
    }
}

QString ItemDefinitionRegistry::normalizeKey(const QString &itemId)
{
    QString key = itemId.startsWith('^') ? itemId.mid(1) : itemId;
    int hashIndex = key.indexOf('#');
    if (hashIndex >= 0) {
        key = key.left(hashIndex);
    }
    return key.toUpper();
}

QString ItemDefinitionRegistry::fallbackKey(const QString &key)
{
    if (key.startsWith("UP_") && key.size() > 3) {
        return QString("U_") + key.mid(3);
    }
    return QString();
}
