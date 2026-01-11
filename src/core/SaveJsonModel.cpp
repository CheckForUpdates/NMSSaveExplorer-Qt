#include "core/SaveJsonModel.h"

#include "core/JsonMapper.h"
#include "core/LosslessJsonDocument.h"
#include "core/ResourceLocator.h"
#include "core/Utf8Diagnostics.h"

#include <QJsonParseError>

namespace SaveJsonModel {
bool ensureMappingLoaded()
{
    if (JsonMapper::isLoaded()) {
        return true;
    }
    QString mappingPath = ResourceLocator::resolveResource("mapping.json");
    return JsonMapper::loadMapping(mappingPath);
}

QVariantList remapPathToShort(const QVariantList &path)
{
    ensureMappingLoaded();
    QHash<QString, QString> reverse;
    const QHash<QString, QString> mapping = JsonMapper::mapping();
    for (auto it = mapping.begin(); it != mapping.end(); ++it) {
        if (!reverse.contains(it.value())) {
            reverse.insert(it.value(), it.key());
        }
    }

    QVariantList out;
    out.reserve(path.size());
    for (const QVariant &segment : path) {
        if (segment.canConvert<QString>()) {
            QString key = segment.toString();
            out << reverse.value(key, key);
        } else {
            out << segment;
        }
    }
    return out;
}

bool setLosslessValue(const std::shared_ptr<LosslessJsonDocument> &lossless,
                      const QVariantList &path, const QJsonValue &value)
{
    if (!lossless) {
        return false;
    }
    if (lossless->setValueAtPath(path, value)) {
        return true;
    }
    QVariantList remapped = remapPathToShort(path);
    if (remapped != path) {
        return lossless->setValueAtPath(remapped, value);
    }
    return false;
}

bool syncRootFromLossless(const std::shared_ptr<LosslessJsonDocument> &lossless,
                          QJsonDocument &rootDoc, QString *errorMessage)
{
    if (!lossless) {
        return false;
    }
    QByteArray json = lossless->toJson(false);
    bool sanitized = false;
    QByteArray qtBytes = sanitizeJsonUtf8ForQt(json, &sanitized);
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(qtBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (errorMessage) {
            *errorMessage = QObject::tr("JSON parse error: %1").arg(parseError.errorString());
        }
        return false;
    }
    if (sanitized) {
        qWarning() << "Sanitized invalid UTF-8 bytes for Qt JSON parser.";
    }
    rootDoc = doc;
    return true;
}
}
