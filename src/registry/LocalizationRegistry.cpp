#include "registry/LocalizationRegistry.h"

#include "core/ResourceLocator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QXmlStreamReader>

QHash<QString, QString> LocalizationRegistry::entries_;
bool LocalizationRegistry::loaded_ = false;

QString LocalizationRegistry::resolveToken(const QString &token)
{
    if (token.isEmpty()) {
        return {};
    }
    ensureLoaded();
    QString key = normalizeKey(token);
    if (key.isEmpty()) {
        return {};
    }
    return entries_.value(key);
}

bool LocalizationRegistry::isLoaded()
{
    return loaded_;
}

void LocalizationRegistry::ensureLoaded()
{
    if (loaded_ && !entries_.isEmpty()) {
        return;
    }
    entries_.clear();
    loadDefinitions();
    loaded_ = !entries_.isEmpty();
}

void LocalizationRegistry::loadDefinitions()
{
    QString root = findLocalizationRoot();
    if (root.isEmpty()) {
        return;
    }

    QDir dir(root);
    QStringList files = dir.entryList(QStringList() << "nms_loc*_usenglish.MXML", QDir::Files);
    for (const QString &file : files) {
        loadLocalizationFile(dir.filePath(file));
    }
}

void LocalizationRegistry::loadLocalizationFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    QXmlStreamReader reader(&file);
    bool inEntry = false;
    int entryDepth = 0;
    int depth = 0;
    QString entryId;
    QString entryText;

    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            ++depth;
            if (reader.name() == QLatin1String("Property")) {
                QXmlStreamAttributes attrs = reader.attributes();
                QString name = attrs.value("name").toString();
                QString value = attrs.value("value").toString();
                if (!inEntry && name == QLatin1String("Table") && value == QLatin1String("TkLocalisationEntry")) {
                    inEntry = true;
                    entryDepth = depth;
                    entryId = attrs.value("_id").toString();
                    entryText.clear();
                    continue;
                }
                if (inEntry && name == QLatin1String("Id")) {
                    if (!value.isEmpty()) {
                        entryId = value;
                    }
                    continue;
                }
                if (inEntry && name == QLatin1String("USEnglish")) {
                    entryText = value;
                    continue;
                }
            }
        } else if (reader.isEndElement()) {
            if (inEntry && reader.name() == QLatin1String("Property") && depth == entryDepth) {
                QString key = normalizeKey(entryId);
                if (!key.isEmpty() && !entryText.isEmpty()) {
                    entries_.insert(key, entryText);
                }
                inEntry = false;
                entryDepth = 0;
                entryId.clear();
                entryText.clear();
            }
            --depth;
        }
    }
}

QString LocalizationRegistry::findLocalizationRoot()
{
    QString resourceRoot = ResourceLocator::resourcesRoot();
    QStringList roots;
    if (!resourceRoot.isEmpty()) {
        roots << resourceRoot;
        QDir resourceDir(resourceRoot);
        QDir parent = resourceDir;
        for (int i = 0; i < 4; ++i) {
            if (!parent.cdUp()) {
                break;
            }
            roots << parent.absolutePath();
        }
    }

    QDir appDir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 8; ++i) {
        roots << appDir.absolutePath();
        if (!appDir.cdUp()) {
            break;
        }
    }

    QDir cwd(QDir::currentPath());
    for (int i = 0; i < 8; ++i) {
        roots << cwd.absolutePath();
        if (!cwd.cdUp()) {
            break;
        }
    }

    roots.removeDuplicates();
    for (const QString &root : roots) {
        QDir dir(root);
        if (dir.exists("localization")) {
            return dir.filePath("localization");
        }
        if (dir.exists("data")) {
            QDir dataDir(dir.filePath("data"));
            QStringList files = dataDir.entryList(QStringList() << "nms_loc*_usenglish.MXML", QDir::Files);
            if (!files.isEmpty()) {
                return dataDir.absolutePath();
            }
        }
        QStringList files = dir.entryList(QStringList() << "nms_loc*_usenglish.MXML", QDir::Files);
        if (!files.isEmpty()) {
            return dir.absolutePath();
        }
    }
    return {};
}

QString LocalizationRegistry::normalizeKey(const QString &key)
{
    QString value = key.trimmed();
    if (value == QLatin1String("^") || value.isEmpty()) {
        return {};
    }
    if (value.startsWith('^')) {
        value = value.mid(1);
    }
    return value.toUpper();
}
