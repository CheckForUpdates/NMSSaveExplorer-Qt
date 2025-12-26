#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

class SaveEncoder
{
public:
    static bool encodeSave(const QString &filePath, const QJsonObject &saveData, QString *errorMessage = nullptr);
    static bool encodeSave(const QString &filePath, const QByteArray &jsonBytes, QString *errorMessage = nullptr);
};
