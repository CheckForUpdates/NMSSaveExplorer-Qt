#include "core/ResourceLocator.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>

namespace {
const char *kEnvResourceRoot = "NMS_SAVE_EXPLORER_RESOURCES";
}

QString ResourceLocator::resourcesRoot()
{
    static QString cached = findResourcesRoot();
    if (!cached.isEmpty()) {
        static bool logged = false;
        if (!logged) {
            logged = true;
            qInfo() << "ResourceLocator using root:" << cached;
        }
    } else {
        qWarning() << "ResourceLocator did not find resources root.";
    }
    return cached;
}

QString ResourceLocator::resolveResource(const QString &relativePath)
{
    QString base = resourcesRoot();
    if (base.isEmpty()) {
        return relativePath;
    }
    QDir dir(base);
    return dir.filePath(relativePath);
}

QString ResourceLocator::findResourcesRoot()
{
    static bool loggedResource = false;
    if (QFile::exists(QStringLiteral(":/resources/mapping.json"))) {
        if (!loggedResource) {
            loggedResource = true;
            qInfo() << "ResourceLocator using bundled resources.";
        }
        return QStringLiteral(":/resources");
    }

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString overrideRoot = env.value(kEnvResourceRoot);
    if (!overrideRoot.isEmpty()) {
        QDir overrideDir(overrideRoot);
        if (overrideDir.exists()) {
            qInfo() << "ResourceLocator using override root:" << overrideDir.absolutePath();
            return overrideDir.absolutePath();
        }
    }

    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 8; ++i) {
        if (dir.exists("src/resources")) {
            qInfo() << "ResourceLocator found src/resources at" << dir.absolutePath();
            return dir.filePath("src/resources");
        }
        if (dir.exists("resources")) {
            qInfo() << "ResourceLocator found resources at" << dir.absolutePath();
            return dir.filePath("resources");
        }
        if (!dir.cdUp()) {
            break;
        }
    }

    return QString();
}
