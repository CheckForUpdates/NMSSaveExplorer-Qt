#pragma once

#include <QDateTime>
#include <QList>
#include <QJsonObject>
#include <QMetaType>
#include <QString>

struct SaveSlot;

struct BackupEntry {
    QString backupPath;
    QString metadataPath;
    QString sourcePath;
    QString saveName;
    QString profileId;
    QString slotId;
    qint64 sourceMtimeMs = 0;
    qint64 backupTimeMs = 0;
    qint64 sizeBytes = 0;
    QString reason;
    QString checksum;
};

Q_DECLARE_METATYPE(BackupEntry)

class BackupManager
{
public:
    explicit BackupManager(const QString &rootPath = QString());

    QString rootPath() const;
    void setRootPath(const QString &path);

    bool createBackup(const QString &sourcePath,
                      const SaveSlot *slot,
                      const QString &reason,
                      BackupEntry *outEntry,
                      QString *errorMessage) const;

    QList<BackupEntry> listBackups(QString *errorMessage = nullptr) const;
    bool restoreBackup(const BackupEntry &entry, const QString &targetPath, QString *errorMessage) const;

    static QString defaultRootPath();
    static QString formatSize(qint64 bytes);
    static QString formatTimestamp(qint64 millisUtc);

private:
    static QString sanitizePathComponent(const QString &value);
    static QString profileIdForSlot(const SaveSlot *slot, const QString &sourcePath);
    static QString slotIdForSlot(const SaveSlot *slot, const QString &sourcePath);
    static QString backupFolderFor(const QString &root,
                                   const QString &profileId,
                                   const QString &slotId,
                                   const QDate &date);
    static BackupEntry entryFromMetadata(const QJsonObject &obj, const QString &metadataPath);

    QString rootPath_;
};
