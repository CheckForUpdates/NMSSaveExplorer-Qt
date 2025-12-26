#include "core/JsonMapper.h"

#include <QFile>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>

namespace {
QHash<QString, QString> g_mapping;
bool g_loaded = false;
}

bool JsonMapper::loadMapping(const QString &path)
{
    qInfo() << "JsonMapper::loadMapping" << path;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        g_loaded = false;
        qWarning() << "JsonMapper failed to open mapping file.";
        return false;
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        g_loaded = false;
        qWarning() << "JsonMapper JSON parse error:" << error.errorString();
        return false;
    }

    return loadMappingFromJson(doc.object());
}

bool JsonMapper::loadMappingFromJson(const QJsonObject &root)
{
    QHash<QString, QString> next;
    if (root.contains("Mapping") && root.value("Mapping").isArray()) {
        QJsonArray arr = root.value("Mapping").toArray();
        for (const QJsonValue &value : arr) {
            if (!value.isObject()) {
                continue;
            }
            QJsonObject pair = value.toObject();
            QString key = pair.value("Key").toString();
            QString val = pair.value("Value").toString();
            if (!key.isEmpty() && !val.isEmpty()) {
                next.insert(key, val);
            }
        }
    } else {
        for (auto it = root.begin(); it != root.end(); ++it) {
            if (it.value().isString()) {
                next.insert(it.key(), it.value().toString());
            }
        }
    }

    setMapping(next);
    return g_loaded;
}

QString JsonMapper::mapKey(const QString &shortKey)
{
    if (!g_loaded) {
        return shortKey;
    }
    return g_mapping.value(shortKey, shortKey);
}

bool JsonMapper::isLoaded()
{
    return g_loaded;
}

int JsonMapper::size()
{
    return g_mapping.size();
}

QHash<QString, QString> JsonMapper::mapping()
{
    return g_mapping;
}

void JsonMapper::setMapping(const QHash<QString, QString> &map)
{
    g_mapping = map;
    g_loaded = true;
    qInfo() << "JsonMapper loaded keys:" << g_mapping.size();
}
