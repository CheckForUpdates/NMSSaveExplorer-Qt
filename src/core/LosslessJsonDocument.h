#pragma once

#include <QByteArray>
#include <QJsonValue>
#include <QString>
#include <QVariantList>

#include <rapidjson/document.h>

class LosslessJsonDocument
{
public:
    bool parse(const QByteArray &json, QString *errorMessage = nullptr);
    QByteArray toJson(bool pretty = false) const;
    bool setValueAtPath(const QVariantList &path, const QJsonValue &value);

    bool isNull() const { return doc_.IsNull(); }
    bool isArray() const { return doc_.IsArray(); }
    bool isObject() const { return doc_.IsObject(); }

private:
    rapidjson::Document doc_;
};
