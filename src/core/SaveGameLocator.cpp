#include "core/SaveGameLocator.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include "core/ManifestManager.h"

namespace {
constexpr int kSearchDepth = 4;
const QRegularExpression kSavePattern(QStringLiteral("^save\\d*\\.hg$"), QRegularExpression::CaseInsensitiveOption);
const QRegularExpression kSaveIndexPattern(QStringLiteral("^save(\\d*)\\.hg$"),
                                            QRegularExpression::CaseInsensitiveOption);

QStringList linuxCandidates()
{
    QStringList roots;
    QString home = QDir::homePath();
    if (!home.isEmpty()) {
        roots << QDir(home).filePath(".local/share/HelloGames/NMS");
        roots << QDir(home).filePath(".config/HelloGames/NMS");

        QStringList steamRoots = {
            QDir(home).filePath(".steam/steam"),
            QDir(home).filePath(".steam/root"),
            QDir(home).filePath(".local/share/Steam"),
            QDir(home).filePath(".steam/debian-installation")
        };
        for (const QString &steamRoot : steamRoots) {
            QDir compat(QDir(steamRoot).filePath("steamapps/compatdata/275850"));
            if (!compat.exists()) {
                continue;
            }
            QDir usersRoot(compat.filePath("pfx/drive_c/users"));
            if (!usersRoot.exists()) {
                continue;
            }
            QFileInfoList users = usersRoot.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QFileInfo &user : users) {
                roots << QDir(user.absoluteFilePath()).filePath("Application Data/HelloGames/NMS");
                roots << QDir(user.absoluteFilePath()).filePath("AppData/Roaming/HelloGames/NMS");
            }
        }
    }
    return roots;
}

QStringList windowsCandidates()
{
    QStringList roots;
    QString appData = qEnvironmentVariable("APPDATA");
    QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    if (!appData.isEmpty()) {
        roots << QDir(appData).filePath("HelloGames/NMS");
    }
    if (!localAppData.isEmpty()) {
        roots << QDir(localAppData).filePath("HelloGames/NMS");
    }
    QString home = QDir::homePath();
    if (!home.isEmpty()) {
        roots << QDir(home).filePath("Saved Games/HelloGames/NMS");
        roots << QDir(home).filePath("Documents/HelloGames/NMS");
    }
    return roots;
}

QStringList macCandidates()
{
    QStringList roots;
    QString home = QDir::homePath();
    if (!home.isEmpty()) {
        roots << QDir(home).filePath("Library/Application Support/HelloGames/NMS");
    }
    return roots;
}

QStringList candidateRoots()
{
#if defined(Q_OS_WIN)
    return windowsCandidates();
#elif defined(Q_OS_MAC)
    return macCandidates();
#elif defined(Q_OS_LINUX)
    return linuxCandidates();
#else
    return QStringList();
#endif
}

QString normalizeKey(const QString &path)
{
#if defined(Q_OS_WIN) || defined(Q_OS_MAC)
    return path.toLower();
#else
    return path;
#endif
}

QString canonicalFolderPath(const QString &path)
{
    QFileInfo info(path);
    QString canonical = info.canonicalFilePath();
    if (!canonical.isEmpty()) {
        return canonical;
    }
    return info.absoluteFilePath();
}

QString slotKeyForFolder(const QString &slotFolder, int groupIndex)
{
    QString base = canonicalFolderPath(slotFolder);
    return normalizeKey(base) + QStringLiteral("::") + QString::number(groupIndex);
}

bool isPrimarySaveFile(const QFileInfo &info)
{
    return kSavePattern.match(info.fileName()).hasMatch();
}

int saveIndexFromFilename(const QString &filename)
{
    QRegularExpressionMatch match = kSaveIndexPattern.match(filename);
    if (!match.hasMatch()) {
        return -1;
    }
    QString digits = match.captured(1);
    if (digits.isEmpty()) {
        return 0;
    }
    bool ok = false;
    int value = digits.toInt(&ok);
    if (!ok || value <= 0) {
        return -1;
    }
    return value - 1;
}

int saveGroupFromIndex(int saveIndex)
{
    if (saveIndex < 0) {
        return -1;
    }
    return saveIndex / 2;
}

struct SlotCandidate {
    QString slotPath;
    QString root;
    QString latestSave;
    qint64 lastModified = std::numeric_limits<qint64>::min();
    QList<SaveSlot::SaveFileEntry> saveFiles;
    QSet<QString> seenPaths;

    void consider(const QFileInfo &info) {
        QString path = info.canonicalFilePath();
        if (path.isEmpty()) {
            path = info.absoluteFilePath();
        }
        if (seenPaths.contains(path)) {
            return;
        }
        seenPaths.insert(path);

        SaveSlot::SaveFileEntry entry;
        entry.filePath = path;
        entry.lastModified = info.lastModified().toMSecsSinceEpoch();
        saveFiles.append(entry);

        qint64 modified = info.lastModified().toMSecsSinceEpoch();
        if (modified > lastModified) {
            lastModified = modified;
            latestSave = info.absoluteFilePath();
        }
    }

    SaveSlot toSaveSlot() const {
        SaveSlot slot;
        slot.slotPath = slotPath;
        slot.rootPath = root;
        slot.latestSave = latestSave;
        slot.lastModified = lastModified;
        slot.saveFiles = saveFiles;
        std::sort(slot.saveFiles.begin(), slot.saveFiles.end(), [](const SaveSlot::SaveFileEntry &a,
                                                                   const SaveSlot::SaveFileEntry &b) {
            if (a.fileName().compare(b.fileName(), Qt::CaseInsensitive) == 0) {
                return a.filePath.toLower() < b.filePath.toLower();
            }
            return a.fileName().toLower() < b.fileName().toLower();
        });

        QFileInfo latestInfo(latestSave);
        QString mfName = latestInfo.fileName().replace("save", "mf_save");
        QString mfPath = QDir(latestInfo.absolutePath()).filePath(mfName);
        
        if (QFile::exists(mfPath)) {
            int slotIdx = 0;
            QRegularExpression numRegex("save(\\d+)\\.hg");
            QRegularExpressionMatch match = numRegex.match(latestInfo.fileName());
            if (match.hasMatch()) {
                slotIdx = match.captured(1).toInt() - 1;
            }
            
            ManifestData mf = ManifestManager::readManifest(mfPath, slotIdx);
            if (mf.isValid()) {
                slot.locationName = mf.locationName;
            }
        }
        return slot;
    }
};
}

QString SaveSlot::displayName() const
{
    if (!rootPath.isEmpty() && slotPath.startsWith(rootPath)) {
        QString relative = QDir(rootPath).relativeFilePath(slotPath);
        if (!relative.isEmpty() && relative != ".") {
            return relative;
        }
    }
    return QFileInfo(slotPath).fileName();
}

QString SaveSlot::latestSaveName() const
{
    return QFileInfo(latestSave).fileName();
}

QString SaveSlot::rootDisplay() const
{
    return rootPath;
}

QList<SaveSlot> SaveGameLocator::discoverSaveSlots()
{
    QHash<QString, SlotCandidate> result;

    for (const QString &root : candidateRoots()) {
        QDir rootDir(root);
        if (!rootDir.exists()) {
            continue;
        }

        QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QFileInfo info(it.next());
        if (!isPrimarySaveFile(info)) {
            continue;
        }
        int saveIndex = saveIndexFromFilename(info.fileName());
        int groupIndex = saveGroupFromIndex(saveIndex);
        if (groupIndex < 0) {
            continue;
        }
        QString slotFolder = info.absolutePath();
        QString key = slotKeyForFolder(slotFolder, groupIndex);
        SlotCandidate &candidate = result[key];
        if (candidate.slotPath.isEmpty()) {
            candidate.slotPath = canonicalFolderPath(slotFolder);
            candidate.root = root;
        }
            candidate.consider(info);
        }

        QFileInfoList children = rootDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo &child : children) {
            if (!child.fileName().startsWith("st_")) {
                continue;
            }
            QDir profileDir(child.absoluteFilePath());
            QDirIterator profileIt(profileDir.path(), QDir::Files, QDirIterator::Subdirectories);
            while (profileIt.hasNext()) {
            QFileInfo info(profileIt.next());
            if (!isPrimarySaveFile(info)) {
                continue;
            }
            int saveIndex = saveIndexFromFilename(info.fileName());
            int groupIndex = saveGroupFromIndex(saveIndex);
            if (groupIndex < 0) {
                continue;
            }
            QString slotFolder = info.absolutePath();
            QString key = slotKeyForFolder(slotFolder, groupIndex);
            SlotCandidate &candidate = result[key];
            if (candidate.slotPath.isEmpty()) {
                candidate.slotPath = canonicalFolderPath(slotFolder);
                candidate.root = root;
            }
                candidate.consider(info);
            }
        }
    }

    QList<SaveSlot> saveSlots;
    saveSlots.reserve(result.size());
    for (const SlotCandidate &candidate : result) {
        SaveSlot slot = candidate.toSaveSlot();
        if (!slot.latestSave.isEmpty()) {
            saveSlots.append(slot);
        }
    }

    std::sort(saveSlots.begin(), saveSlots.end(), [](const SaveSlot &a, const SaveSlot &b) {
        if (a.lastModified == b.lastModified) {
            return a.displayName().toLower() < b.displayName().toLower();
        }
        return a.lastModified > b.lastModified;
    });

    return saveSlots;
}

QList<SaveSlot> SaveGameLocator::scanDirectory(const QString &path)
{
    QHash<QString, SlotCandidate> result;
    QDir rootDir(path);
    if (!rootDir.exists()) {
        return {};
    }

    QDirIterator it(path, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QFileInfo info(it.next());
        if (!isPrimarySaveFile(info)) {
            continue;
        }
        int saveIndex = saveIndexFromFilename(info.fileName());
        int groupIndex = saveGroupFromIndex(saveIndex);
        if (groupIndex < 0) {
            continue;
        }
        QString slotFolder = info.absolutePath();
        QString key = slotKeyForFolder(slotFolder, groupIndex);
        SlotCandidate &candidate = result[key];
        if (candidate.slotPath.isEmpty()) {
            candidate.slotPath = canonicalFolderPath(slotFolder);
            candidate.root = path;
        }
        candidate.consider(info);
    }

    QList<SaveSlot> saveSlots;
    saveSlots.reserve(result.size());
    for (const SlotCandidate &candidate : result) {
        SaveSlot slot = candidate.toSaveSlot();
        if (!slot.latestSave.isEmpty()) {
            saveSlots.append(slot);
        }
    }

    std::sort(saveSlots.begin(), saveSlots.end(), [](const SaveSlot &a, const SaveSlot &b) {
        if (a.lastModified == b.lastModified) {
            return a.displayName().toLower() < b.displayName().toLower();
        }
        return a.lastModified > b.lastModified;
    });

    return saveSlots;
}
