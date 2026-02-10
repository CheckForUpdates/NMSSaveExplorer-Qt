#pragma once

#include <QByteArray>
#include <QJsonDocument>
#include <QString>
#include <memory>

class LosslessJsonDocument;

class SaveCache
{
public:
    static bool load(const QString &filePath, QByteArray *bytes, QJsonDocument *doc,
                     QString *errorMessage = nullptr);
    static bool loadWithLossless(const QString &filePath, QByteArray *bytes, QJsonDocument *doc,
                                 std::shared_ptr<LosslessJsonDocument> *lossless,
                                 QString *errorMessage = nullptr);
    static void clear();
};
