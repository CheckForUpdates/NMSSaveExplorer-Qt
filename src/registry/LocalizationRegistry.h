#pragma once

#include <QHash>
#include <QString>

class LocalizationRegistry
{
public:
    static QString resolveToken(const QString &token);
    static bool isLoaded();

private:
    static void ensureLoaded();
    static void loadDefinitions();
    static void loadLocalizationFile(const QString &path);
    static QString findLocalizationRoot();
    static QString normalizeKey(const QString &key);

    static QHash<QString, QString> entries_;
    static bool loaded_;
};
