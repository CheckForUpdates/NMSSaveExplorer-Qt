#pragma once

#include <QJsonDocument>
#include <QVariantList>
#include <memory>

class LosslessJsonDocument;

namespace SaveJsonModel {
bool ensureMappingLoaded();
QVariantList remapPathToShort(const QVariantList &path);
bool setLosslessValue(const std::shared_ptr<LosslessJsonDocument> &lossless,
                      const QVariantList &path, const QJsonValue &value);
bool syncRootFromLossless(const std::shared_ptr<LosslessJsonDocument> &lossless,
                          QJsonDocument &rootDoc, QString *errorMessage = nullptr);
}
