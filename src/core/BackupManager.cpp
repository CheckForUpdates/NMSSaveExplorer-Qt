#include "core/BackupManager.h"

#include "core/SaveGameLocator.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

namespace {
constexpr int kSecondsPerMinute = 60;
constexpr int kMinutesPerHour = 60;
constexpr int kHoursPerDay = 24;
constexpr qint64 kBytesPerKiB = 1024;
constexpr qint64 kBytesPerMiB = 1024 * 1024;
constexpr qint64 kBytesPerGiB = 1024 * 1024 * 1024;

QString normalizeSeparators(const QString &value)
{
    QString out = QDir::cleanPath(value);
    return out.replace("\\", "/");
}
}

BackupManager::BackupManager(const QString &rootPath)
    : rootPath_(rootPath)
{
    if (rootPath_.isEmpty()) {
        rootPath_ = defaultRootPath();
    }
}

QString BackupManager::rootPath() const
{
    return rootPath_;
}

void BackupManager::setRootPath(const QString &path)
{
    rootPath_ = path;
}

bool BackupManager::createBackup(const QString &sourcePath,
                                 const SaveSlot *slot,
                                 const QString &reason,
                                 BackupEntry *outEntry,
                                 QString *errorMessage) const
{
    QFileInfo info(sourcePath);
    if (!info.exists() || !info.isFile()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Save file not found.");
        }
        return false;
    }

    QString profileId = profileIdForSlot(slot, sourcePath);
    QString slotId = slotIdForSlot(slot, sourcePath);
    QDate today = QDate::currentDate();
    QString folder = backupFolderFor(rootPath_, profileId, slotId, today);
    QDir dir(folder);
    if (!dir.exists() && !dir.mkpath(".")) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to create backup folder.");
        }
        return false;
    }

    QString baseName = info.completeBaseName();
    if (baseName.isEmpty()) {
        baseName = QStringLiteral("save");
    }
    QString extension = info.suffix();
    if (extension.isEmpty()) {
        extension = QStringLiteral("hg");
    }
    QString timestamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    QString backupFileName = QStringLiteral("%1_%2.%3").arg(baseName, timestamp, extension);
    QString backupPath = dir.filePath(backupFileName);
    QString metadataPath = backupPath + QStringLiteral(".json");

    QFile sourceFile(sourcePath);
    if (!sourceFile.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to read save file.");
        }
        return false;
    }
    QByteArray bytes = sourceFile.readAll();
    sourceFile.close();
    if (bytes.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Save file is empty.");
        }
        return false;
    }

    QSaveFile outFile(backupPath);
    if (!outFile.open(QIODevice::WriteOnly)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to write backup file.");
        }
        return false;
    }
    if (outFile.write(bytes) != bytes.size()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to write backup file.");
        }
        return false;
    }
    if (!outFile.commit()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to finalize backup file.");
        }
        return false;
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(bytes);
    QString checksum = QString::fromLatin1(hash.result().toHex());

    BackupEntry entry;
    entry.backupPath = backupPath;
    entry.metadataPath = metadataPath;
    entry.sourcePath = sourcePath;
    entry.saveName = info.fileName();
    entry.profileId = profileId;
    entry.slotId = slotId;
    entry.sourceMtimeMs = info.lastModified().toMSecsSinceEpoch();
    entry.backupTimeMs = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    entry.sizeBytes = bytes.size();
    entry.reason = reason;
    entry.checksum = checksum;

    QJsonObject meta;
    meta.insert(QStringLiteral("backupFile"), QFileInfo(backupPath).fileName());
    meta.insert(QStringLiteral("sourcePath"), entry.sourcePath);
    meta.insert(QStringLiteral("saveName"), entry.saveName);
    meta.insert(QStringLiteral("profileId"), entry.profileId);
    meta.insert(QStringLiteral("slotId"), entry.slotId);
    meta.insert(QStringLiteral("sourceMtimeMs"), static_cast<double>(entry.sourceMtimeMs));
    meta.insert(QStringLiteral("backupTimeMs"), static_cast<double>(entry.backupTimeMs));
    meta.insert(QStringLiteral("sizeBytes"), static_cast<double>(entry.sizeBytes));
    meta.insert(QStringLiteral("reason"), entry.reason);
    meta.insert(QStringLiteral("checksum"), entry.checksum);
    meta.insert(QStringLiteral("appVersion"), QCoreApplication::applicationVersion());

    QSaveFile metaFile(metadataPath);
    if (!metaFile.open(QIODevice::WriteOnly)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to write backup metadata.");
        }
        return false;
    }
    QJsonDocument doc(meta);
    metaFile.write(doc.toJson(QJsonDocument::Indented));
    if (!metaFile.commit()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to finalize backup metadata.");
        }
        return false;
    }

    if (outEntry) {
        *outEntry = entry;
    }
    return true;
}

QList<BackupEntry> BackupManager::listBackups(QString *errorMessage) const
{
    QList<BackupEntry> entries;
    QDir root(rootPath_);
    if (!root.exists()) {
        return entries;
    }

    QDirIterator it(rootPath_, QStringList() << QStringLiteral("*.json"),
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString metadataPath = it.next();
        QFile file(metadataPath);
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        file.close();
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            continue;
        }

        BackupEntry entry = entryFromMetadata(doc.object(), metadataPath);
        if (entry.backupPath.isEmpty()) {
            QString guessed = metadataPath;
            if (guessed.endsWith(QStringLiteral(".json"))) {
                guessed.chop(5);
            }
            entry.backupPath = guessed;
        }
        QFileInfo backupInfo(entry.backupPath);
        if (entry.sizeBytes <= 0 && backupInfo.exists()) {
            entry.sizeBytes = backupInfo.size();
        }
        entries.append(entry);
    }

    std::sort(entries.begin(), entries.end(), [](const BackupEntry &a, const BackupEntry &b) {
        return a.backupTimeMs > b.backupTimeMs;
    });

    if (errorMessage) {
        errorMessage->clear();
    }
    return entries;
}

bool BackupManager::restoreBackup(const BackupEntry &entry, const QString &targetPath, QString *errorMessage) const
{
    QFile source(entry.backupPath);
    if (!source.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to read backup file.");
        }
        return false;
    }

    QByteArray bytes = source.readAll();
    source.close();
    if (bytes.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Backup file is empty.");
        }
        return false;
    }

    QSaveFile target(targetPath);
    if (!target.open(QIODevice::WriteOnly)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to write target save.");
        }
        return false;
    }
    if (target.write(bytes) != bytes.size()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to write target save.");
        }
        return false;
    }
    if (!target.commit()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to finalize restored save.");
        }
        return false;
    }
    return true;
}

QString BackupManager::defaultRootPath()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) {
        base = QDir::homePath();
    }
    return QDir(base).filePath(QStringLiteral("backups"));
}

QString BackupManager::formatSize(qint64 bytes)
{
    if (bytes >= kBytesPerGiB) {
        return QStringLiteral("%1 GiB").arg(QString::number(bytes / static_cast<double>(kBytesPerGiB), 'f', 2));
    }
    if (bytes >= kBytesPerMiB) {
        return QStringLiteral("%1 MiB").arg(QString::number(bytes / static_cast<double>(kBytesPerMiB), 'f', 2));
    }
    if (bytes >= kBytesPerKiB) {
        return QStringLiteral("%1 KiB").arg(QString::number(bytes / static_cast<double>(kBytesPerKiB), 'f', 1));
    }
    return QStringLiteral("%1 B").arg(bytes);
}

QString BackupManager::formatTimestamp(qint64 millisUtc)
{
    if (millisUtc <= 0) {
        return QStringLiteral("Unknown");
    }
    QDateTime dt = QDateTime::fromMSecsSinceEpoch(millisUtc, Qt::UTC).toLocalTime();
    return dt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

QString BackupManager::sanitizePathComponent(const QString &value)
{
    QString normalized = normalizeSeparators(value).trimmed();
    normalized.replace("/", "_");
    normalized.replace(" ", "_");
    normalized.replace(":", "_");
    if (normalized.isEmpty()) {
        return QStringLiteral("unknown");
    }
    return normalized;
}

QString BackupManager::profileIdForSlot(const SaveSlot *slot, const QString &sourcePath)
{
    if (slot && !slot->rootPath.isEmpty() && slot->slotPath.startsWith(slot->rootPath)) {
        QString relative = QDir(slot->rootPath).relativeFilePath(slot->slotPath);
        relative = normalizeSeparators(relative);
        QStringList parts = relative.split("/", Qt::SkipEmptyParts);
        if (!parts.isEmpty() && parts.first().startsWith("st_")) {
            return sanitizePathComponent(parts.first());
        }
    }
    QFileInfo info(sourcePath);
    return sanitizePathComponent(info.absolutePath());
}

QString BackupManager::slotIdForSlot(const SaveSlot *slot, const QString &sourcePath)
{
    if (slot && !slot->rootPath.isEmpty() && slot->slotPath.startsWith(slot->rootPath)) {
        QString relative = QDir(slot->rootPath).relativeFilePath(slot->slotPath);
        relative = normalizeSeparators(relative);
        QStringList parts = relative.split("/", Qt::SkipEmptyParts);
        if (!parts.isEmpty() && parts.first().startsWith("st_")) {
            parts.removeFirst();
        }
        if (!parts.isEmpty()) {
            return sanitizePathComponent(parts.join("_"));
        }
    }
    QFileInfo info(sourcePath);
    return sanitizePathComponent(info.completeBaseName());
}

QString BackupManager::backupFolderFor(const QString &root,
                                       const QString &profileId,
                                       const QString &slotId,
                                       const QDate &date)
{
    QDir rootDir(root);
    QString datePath = QStringLiteral("%1/%2/%3")
                           .arg(date.year(), 4, 10, QLatin1Char('0'))
                           .arg(date.month(), 2, 10, QLatin1Char('0'))
                           .arg(date.day(), 2, 10, QLatin1Char('0'));
    return rootDir.filePath(QStringLiteral("profiles/%1/slots/%2/%3")
                                .arg(sanitizePathComponent(profileId),
                                     sanitizePathComponent(slotId),
                                     datePath));
}

BackupEntry BackupManager::entryFromMetadata(const QJsonObject &obj, const QString &metadataPath)
{
    BackupEntry entry;
    entry.metadataPath = metadataPath;
    entry.sourcePath = obj.value(QStringLiteral("sourcePath")).toString();
    entry.saveName = obj.value(QStringLiteral("saveName")).toString();
    entry.profileId = obj.value(QStringLiteral("profileId")).toString();
    entry.slotId = obj.value(QStringLiteral("slotId")).toString();
    entry.sourceMtimeMs = static_cast<qint64>(obj.value(QStringLiteral("sourceMtimeMs")).toDouble());
    entry.backupTimeMs = static_cast<qint64>(obj.value(QStringLiteral("backupTimeMs")).toDouble());
    entry.sizeBytes = static_cast<qint64>(obj.value(QStringLiteral("sizeBytes")).toDouble());
    entry.reason = obj.value(QStringLiteral("reason")).toString();
    entry.checksum = obj.value(QStringLiteral("checksum")).toString();

    QString backupFile = obj.value(QStringLiteral("backupFile")).toString();
    if (!backupFile.isEmpty()) {
        entry.backupPath = QDir(QFileInfo(metadataPath).absolutePath()).filePath(backupFile);
    }
    return entry;
}
