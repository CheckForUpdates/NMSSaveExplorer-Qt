#include "core/SaveCache.h"

#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QJsonParseError>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>

#include "core/SaveDecoder.h"
#include "core/Utf8Diagnostics.h"
#include "core/LosslessJsonDocument.h"

namespace {
struct SaveCacheEntry {
    QString canonicalPath;
    qint64 mtime = 0;
    qint64 size = 0;
    QByteArray bytes;
    QJsonDocument doc;
    std::shared_ptr<LosslessJsonDocument> lossless;
    bool valid = false;
};

SaveCacheEntry g_cache;
QMutex g_cacheMutex;

QString canonicalizePath(const QString &filePath, QFileInfo &info)
{
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}
}

bool SaveCache::load(const QString &filePath, QByteArray *bytes, QJsonDocument *doc,
                     QString *errorMessage)
{
    if (bytes) {
        bytes->clear();
    }
    if (doc) {
        *doc = QJsonDocument();
    }

    QFileInfo info(filePath);
    if (!info.exists()) {
        if (errorMessage) {
            *errorMessage = QObject::tr("Unable to open %1").arg(filePath);
        }
        return false;
    }

    const QString canonical = canonicalizePath(filePath, info);
    const qint64 mtime = info.lastModified().toMSecsSinceEpoch();
    const qint64 size = info.size();

    {
        QMutexLocker locker(&g_cacheMutex);
        if (g_cache.valid && g_cache.canonicalPath == canonical
            && g_cache.mtime == mtime && g_cache.size == size) {
            if (bytes) {
                *bytes = g_cache.bytes;
            }
            if (doc) {
                *doc = g_cache.doc;
            }
            return true;
        }
    }

    QByteArray contentBytes;
    if (filePath.endsWith(".hg", Qt::CaseInsensitive)) {
        contentBytes = SaveDecoder::decodeSaveBytes(filePath, errorMessage);
    } else {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            if (errorMessage) {
                *errorMessage = QObject::tr("Unable to open %1").arg(filePath);
            }
            return false;
        }
        contentBytes = file.readAll();
    }

    if (contentBytes.isEmpty()) {
        if (errorMessage && errorMessage->isEmpty()) {
            *errorMessage = QObject::tr("No data loaded from %1").arg(filePath);
        }
        return false;
    }

    bool sanitized = false;
    QByteArray qtBytes = sanitizeJsonUtf8ForQt(contentBytes, &sanitized);
    QJsonParseError parseError;
    QJsonDocument parsed = QJsonDocument::fromJson(qtBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (errorMessage) {
            *errorMessage = QObject::tr("JSON parse error: %1").arg(parseError.errorString());
        }
        logJsonUtf8Error(qtBytes, static_cast<int>(parseError.offset));
        return false;
    }
    if (sanitized) {
        qWarning() << "Sanitized invalid UTF-8 bytes for Qt JSON parser.";
    }

    {
        QMutexLocker locker(&g_cacheMutex);
        g_cache.canonicalPath = canonical;
        g_cache.mtime = mtime;
        g_cache.size = size;
        g_cache.bytes = contentBytes;
        g_cache.doc = parsed;
        g_cache.valid = true;
    }

    if (bytes) {
        *bytes = contentBytes;
    }
    if (doc) {
        *doc = parsed;
    }
    return true;
}

bool SaveCache::loadWithLossless(const QString &filePath, QByteArray *bytes, QJsonDocument *doc,
                                 std::shared_ptr<LosslessJsonDocument> *lossless,
                                 QString *errorMessage)
{
    QByteArray localBytes;
    QJsonDocument localDoc;
    QByteArray *bytesOut = bytes ? bytes : &localBytes;
    QJsonDocument *docOut = doc ? doc : &localDoc;

    if (lossless) {
        lossless->reset();
    }

    if (!load(filePath, bytesOut, docOut, errorMessage)) {
        return false;
    }

    QFileInfo info(filePath);
    const QString canonical = canonicalizePath(filePath, info);
    const qint64 mtime = info.lastModified().toMSecsSinceEpoch();
    const qint64 size = info.size();

    {
        QMutexLocker locker(&g_cacheMutex);
        if (g_cache.valid && g_cache.canonicalPath == canonical
            && g_cache.mtime == mtime && g_cache.size == size
            && g_cache.lossless) {
            if (lossless) {
                *lossless = g_cache.lossless->clone();
            }
            return true;
        }
    }

    auto parsedLossless = std::make_shared<LosslessJsonDocument>();
    if (!parsedLossless->parse(*bytesOut, errorMessage)) {
        return false;
    }

    {
        QMutexLocker locker(&g_cacheMutex);
        if (g_cache.valid && g_cache.canonicalPath == canonical
            && g_cache.mtime == mtime && g_cache.size == size) {
            g_cache.lossless = parsedLossless;
        }
    }

    if (lossless) {
        *lossless = parsedLossless->clone();
    }
    return true;
}

void SaveCache::clear()
{
    QMutexLocker locker(&g_cacheMutex);
    g_cache = SaveCacheEntry();
}
