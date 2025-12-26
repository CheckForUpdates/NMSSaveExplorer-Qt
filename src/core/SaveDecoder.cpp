#include "core/SaveDecoder.h"

#include <QByteArray>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <limits>

#include "lz4.h"

namespace {
constexpr quint32 kMagic = 0xFEEDA1E5;
constexpr qint64 kMaxChunkSize = 128 * 1024 * 1024;

bool debugSaveEnabled()
{
    return qEnvironmentVariableIntValue("NMSSE_DEBUG_SAVE") == 1;
}

quint32 readLe32(const QByteArray &data, int offset)
{
    const unsigned char *ptr = reinterpret_cast<const unsigned char *>(data.constData() + offset);
    return static_cast<quint32>(ptr[0])
           | (static_cast<quint32>(ptr[1]) << 8)
           | (static_cast<quint32>(ptr[2]) << 16)
           | (static_cast<quint32>(ptr[3]) << 24);
}
}

QString SaveDecoder::decodeSave(const QString &filePath, QString *errorMessage)
{
    QByteArray bytes = decodeSaveBytes(filePath, errorMessage);
    if (bytes.isEmpty()) {
        return QString();
    }
    return QString::fromUtf8(bytes);
}

QByteArray SaveDecoder::decodeSaveBytes(const QString &filePath, QString *errorMessage)
{
    if (debugSaveEnabled()) {
        QFileInfo info(filePath);
        QString canonical = info.canonicalFilePath();
        qInfo() << "SaveDecoder path" << filePath << "canonical" << (canonical.isEmpty() ? info.absoluteFilePath() : canonical);
    }
    if (debugSaveEnabled()) {
        qInfo() << "SaveDecoder::decodeSave" << filePath;
    }
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = QString("Unable to open %1").arg(filePath);
        }
        return QByteArray();
    }

    QByteArray data = file.readAll();
    if (debugSaveEnabled()) {
        qInfo() << "Save file size:" << data.size();
    }
    QByteArray output;

    // Find the first occurrence of kMagic to skip any external header
    qint64 offset = -1;
    for (int i = 0; i + 4 <= data.size(); ++i) {
        if (readLe32(data, i) == kMagic) {
            offset = i;
            break;
        }
    }

    if (offset < 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Invalid .hg file: magic header not found");
        }
        return QByteArray();
    }

    const qint64 dataSize = data.size();
    while (offset + 16 <= dataSize) {
        quint32 magic = readLe32(data, static_cast<int>(offset));
        if (magic != kMagic) {
            qWarning() << "SaveDecoder magic mismatch at offset" << offset << "magic=" << Qt::hex
                       << magic << Qt::dec;
            break;
        }
        quint32 compressedSize = readLe32(data, static_cast<int>(offset + 4));
        quint32 uncompressedSize = readLe32(data, static_cast<int>(offset + 8));
        if (debugSaveEnabled()) {
            qInfo() << "Chunk sizes:" << compressedSize << uncompressedSize;
        }
        offset += 16;

        if (compressedSize == 0 && uncompressedSize == 0) {
            if (debugSaveEnabled()) {
                qInfo() << "End of save data reached (terminal chunk)";
            }
            break;
        }

        if (compressedSize == 0 || uncompressedSize == 0) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Invalid save chunk size");
            }
            return QByteArray();
        }
        if (compressedSize > static_cast<quint32>(std::numeric_limits<int>::max())
            || uncompressedSize > static_cast<quint32>(std::numeric_limits<int>::max())) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Save chunk too large");
            }
            return QByteArray();
        }
        if (compressedSize > static_cast<quint32>(kMaxChunkSize)
            || uncompressedSize > static_cast<quint32>(kMaxChunkSize)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Save chunk exceeds size limits");
            }
            return QByteArray();
        }
        if (offset + static_cast<qint64>(compressedSize) > dataSize) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Save chunk exceeds file size");
            }
            return QByteArray();
        }

        QByteArray decompressed;
        decompressed.resize(static_cast<int>(uncompressedSize));
        int decoded = LZ4_decompress_safe(data.constData() + offset, decompressed.data(),
                                          static_cast<int>(compressedSize),
                                          static_cast<int>(uncompressedSize));
        if (decoded < 0) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("LZ4 decompression failed");
            }
            return QByteArray();
        }
        if (decoded != static_cast<int>(uncompressedSize)) {
            qWarning() << "SaveDecoder: decoded size mismatch. Expected" << uncompressedSize << "but got" << decoded;
        }
        output.append(decompressed.left(decoded));
        offset += static_cast<qint64>(compressedSize);
    }

    int lastObject = output.lastIndexOf('}');
    int lastArray = output.lastIndexOf(']');
    int lastGood = qMax(lastObject, lastArray);
    QByteArray cleaned = output;
    if (lastGood >= 0) {
        cleaned = output.left(lastGood + 1);
    }

    if (debugSaveEnabled()) {
        if (lastGood >= 0 && lastGood + 1 < output.size()) {
            QByteArray tail = output.mid(lastGood + 1);
            int nonNullCount = 0;
            for (char c : tail) {
                if (c != '\0') {
                    nonNullCount++;
                }
            }
            if (nonNullCount > 0) {
                qInfo() << "SaveDecoder trailing bytes after JSON:" << tail.size()
                        << "non-null count=" << nonNullCount;
            }
        }
        static const QRegularExpression kLargeIntPattern(QStringLiteral("-?\\d{16,}"));
        QRegularExpressionMatchIterator it = kLargeIntPattern.globalMatch(cleaned);
        int count = 0;
        QStringList samples;
        while (it.hasNext()) {
            QRegularExpressionMatch match = it.next();
            if (!match.hasMatch()) {
                continue;
            }
            if (samples.size() < 5) {
                samples.append(match.captured(0));
            }
            ++count;
        }
        if (count > 0) {
            qInfo() << "SaveDecoder large integer literals:" << count << "samples:" << samples;
        }
    }

    return cleaned;
}
