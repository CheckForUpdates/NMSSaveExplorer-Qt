#include "core/LosslessJsonDocument.h"

#include <cmath>
#include <limits>

#include <QJsonArray>
#include <QJsonObject>

#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace {
rapidjson::Value toRapidValue(const QJsonValue &value, rapidjson::Document::AllocatorType &alloc);

rapidjson::Value toRapidNumberForExisting(const QJsonValue &value, const rapidjson::Value &existing,
                                          rapidjson::Document::AllocatorType &alloc)
{
    if (!value.isDouble()) {
        return toRapidValue(value, alloc);
    }
    double number = value.toDouble();
    if (existing.IsInt64()) {
        rapidjson::Value out;
        out.SetInt64(static_cast<qint64>(number));
        return out;
    }
    if (existing.IsUint64()) {
        rapidjson::Value out;
        out.SetUint64(static_cast<quint64>(number));
        return out;
    }
    if (existing.IsInt()) {
        rapidjson::Value out;
        out.SetInt(static_cast<int>(number));
        return out;
    }
    if (existing.IsUint()) {
        rapidjson::Value out;
        out.SetUint(static_cast<unsigned int>(number));
        return out;
    }
    if (existing.IsDouble()) {
        rapidjson::Value out;
        out.SetDouble(number);
        return out;
    }
    return toRapidValue(value, alloc);
}

rapidjson::Value toRapidValue(const QJsonValue &value, rapidjson::Document::AllocatorType &alloc)
{
    rapidjson::Value out;
    if (value.isNull() || value.isUndefined()) {
        out.SetNull();
        return out;
    }
    if (value.isBool()) {
        out.SetBool(value.toBool());
        return out;
    }
    if (value.isDouble()) {
        double number = value.toDouble();
        double intPart = 0.0;
        if (std::modf(number, &intPart) == 0.0) {
            if (number >= static_cast<double>(std::numeric_limits<qint64>::min())
                && number <= static_cast<double>(std::numeric_limits<qint64>::max())) {
                out.SetInt64(static_cast<qint64>(intPart));
                return out;
            }
            if (number >= 0.0
                && number <= static_cast<double>(std::numeric_limits<quint64>::max())) {
                out.SetUint64(static_cast<quint64>(intPart));
                return out;
            }
        }
        out.SetDouble(number);
        return out;
    }
    if (value.isString()) {
        QByteArray utf8 = value.toString().toUtf8();
        out.SetString(utf8.constData(), static_cast<rapidjson::SizeType>(utf8.size()), alloc);
        return out;
    }
    if (value.isArray()) {
        out.SetArray();
        QJsonArray arr = value.toArray();
        out.Reserve(arr.size(), alloc);
        for (const QJsonValue &entry : arr) {
            out.PushBack(toRapidValue(entry, alloc), alloc);
        }
        return out;
    }
    if (value.isObject()) {
        out.SetObject();
        QJsonObject obj = value.toObject();
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            QByteArray key = it.key().toUtf8();
            rapidjson::Value keyValue;
            keyValue.SetString(key.constData(), static_cast<rapidjson::SizeType>(key.size()), alloc);
            out.AddMember(keyValue, toRapidValue(it.value(), alloc), alloc);
        }
        return out;
    }
    out.SetNull();
    return out;
}
}

bool LosslessJsonDocument::parse(const QByteArray &json, QString *errorMessage)
{
    doc_.Parse<rapidjson::kParseFullPrecisionFlag>(json.constData());
    if (doc_.HasParseError()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("RapidJSON parse error at offset %1")
                                .arg(doc_.GetErrorOffset());
        }
        return false;
    }
    return true;
}

QByteArray LosslessJsonDocument::toJson(bool pretty) const
{
    rapidjson::StringBuffer buffer;
    if (pretty) {
        rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
        doc_.Accept(writer);
    } else {
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        doc_.Accept(writer);
    }
    return QByteArray(buffer.GetString(), static_cast<int>(buffer.GetSize()));
}

bool LosslessJsonDocument::setValueAtPath(const QVariantList &path, const QJsonValue &value)
{
    if (path.isEmpty()) {
        return false;
    }

    rapidjson::Value *node = &doc_;
    for (int i = 0; i < path.size() - 1; ++i) {
        const QVariant &segment = path.at(i);
        if (segment.canConvert<int>() && node->IsArray()) {
            int index = segment.toInt();
            if (index < 0 || index >= static_cast<int>(node->Size())) {
                return false;
            }
            node = &(*node)[static_cast<rapidjson::SizeType>(index)];
            continue;
        }
        if (segment.canConvert<QString>() && node->IsObject()) {
            QByteArray key = segment.toString().toUtf8();
            if (!node->HasMember(key.constData())) {
                return false;
            }
            node = &(*node)[key.constData()];
            continue;
        }
        return false;
    }

    rapidjson::Document::AllocatorType &alloc = doc_.GetAllocator();
    const QVariant &leaf = path.last();
    if (leaf.canConvert<int>() && node->IsArray()) {
        int index = leaf.toInt();
        if (index < 0 || index >= static_cast<int>(node->Size())) {
            return false;
        }
        rapidjson::Value &existing = (*node)[static_cast<rapidjson::SizeType>(index)];
        rapidjson::Value newValue = toRapidNumberForExisting(value, existing, alloc);
        existing = newValue;
        return true;
    }
    if (leaf.canConvert<QString>() && node->IsObject()) {
        QByteArray key = leaf.toString().toUtf8();
        if (node->HasMember(key.constData())) {
            rapidjson::Value &existing = (*node)[key.constData()];
            rapidjson::Value newValue = toRapidNumberForExisting(value, existing, alloc);
            existing = newValue;
        } else {
            rapidjson::Value newValue = toRapidValue(value, alloc);
            rapidjson::Value keyValue;
            keyValue.SetString(key.constData(), static_cast<rapidjson::SizeType>(key.size()), alloc);
            node->AddMember(keyValue, newValue, alloc);
        }
        return true;
    }
    return false;
}

std::shared_ptr<LosslessJsonDocument> LosslessJsonDocument::clone() const
{
    auto copy = std::make_shared<LosslessJsonDocument>();
    copy->doc_.CopyFrom(doc_, copy->doc_.GetAllocator());
    return copy;
}
