#pragma once

#include <QDateTime>
#include <QFileInfo>
#include <QList>
#include <QMetaType>
#include <QString>

struct SaveSlot
{
    struct SaveFileEntry
    {
        QString filePath;
        qint64 lastModified = 0;

        QString fileName() const { return QFileInfo(filePath).fileName(); }
    };

    QString slotPath;
    QString rootPath;
    QString latestSave;
    qint64 lastModified = 0;
    QList<SaveFileEntry> saveFiles;
    
    QString locationName;
    QString saveName;
    QString playTime;

    QString displayName() const;
    QString latestSaveName() const;
    QString rootDisplay() const;
};

class SaveGameLocator
{
public:
    static QList<SaveSlot> discoverSaveSlots();
    static QList<SaveSlot> scanDirectory(const QString &path);
};

Q_DECLARE_METATYPE(SaveSlot)
