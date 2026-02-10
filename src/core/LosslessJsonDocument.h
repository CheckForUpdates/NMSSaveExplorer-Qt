#pragma once

#include <QByteArray>
#include <QJsonValue>
#include <QString>
#include <QVariantList>
#include <memory>

#include <rapidjson/document.h>

class LosslessJsonDocument
{
public:
    bool parse(const QByteArray &json, QString *errorMessage = nullptr);
    QByteArray toJson(bool pretty = false) const;
    bool setValueAtPath(const QVariantList &path, const QJsonValue &value);
    std::shared_ptr<LosslessJsonDocument> clone() const;

    bool isNull() const { return doc_.IsNull(); }
    bool isArray() const { return doc_.IsArray(); }
    bool isObject() const { return doc_.IsObject(); }
    const rapidjson::Value &root() const { return doc_; }

private:
    rapidjson::Document doc_;
};
