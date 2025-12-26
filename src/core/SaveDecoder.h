#pragma once

#include <QString>

class SaveDecoder
{
public:
    static QString decodeSave(const QString &filePath, QString *errorMessage = nullptr);
    static QByteArray decodeSaveBytes(const QString &filePath, QString *errorMessage = nullptr);
};
