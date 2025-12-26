#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>

class JsonMapper
{
public:
    static bool loadMapping(const QString &path);
    static bool loadMappingFromJson(const QJsonObject &root);
    static QString mapKey(const QString &shortKey);
    static bool isLoaded();
    static int size();
    static QHash<QString, QString> mapping();

private:
    static void setMapping(const QHash<QString, QString> &map);
};
