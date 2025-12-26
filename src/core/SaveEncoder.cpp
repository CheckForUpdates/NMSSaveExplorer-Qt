#include "core/SaveEncoder.h"

#include <QByteArray>
#include <QFile>
#include <QJsonDocument>

#include "lz4.h"
#include "core/ManifestManager.h"
#include <QDir>
#include <QRegularExpression>

namespace {
constexpr quint32 kMagic = 0xFEEDA1E5;
constexpr int kDefaultBlockSize = 0x10000;

QByteArray intToLe32(quint32 value)
{
    QByteArray bytes;
    bytes.resize(4);
    bytes[0] = static_cast<char>(value & 0xFF);
    bytes[1] = static_cast<char>((value >> 8) & 0xFF);
    bytes[2] = static_cast<char>((value >> 16) & 0xFF);
    bytes[3] = static_cast<char>((value >> 24) & 0xFF);
    return bytes;
}

quint32 readLe32(const QByteArray &data, int offset)
{
    const unsigned char *ptr = reinterpret_cast<const unsigned char *>(data.constData() + offset);
    return static_cast<quint32>(ptr[0])
           | (static_cast<quint32>(ptr[1]) << 8)
           | (static_cast<quint32>(ptr[2]) << 16)
           | (static_cast<quint32>(ptr[3]) << 24);
}

int findHeaderEnd(const QByteArray &data)
{
    for (int i = 0; i + 4 <= data.size(); ++i) {
        quint32 value = static_cast<unsigned char>(data[i])
                        | (static_cast<unsigned char>(data[i + 1]) << 8)
                        | (static_cast<unsigned char>(data[i + 2]) << 16)
                        | (static_cast<unsigned char>(data[i + 3]) << 24);
        if (value == kMagic) {
            return i;
        }
    }
    return -1;
}

struct BlockFormatInfo {
    int blockSize = kDefaultBlockSize;
    bool padToBlock = false;
    bool hasTerminalChunk = false;
    int trailingNulls = 0;
};

BlockFormatInfo detectBlockFormat(const QByteArray &data, int headerEnd)
{
    BlockFormatInfo info;
    if (headerEnd < 0 || headerEnd + 16 > data.size()) {
        return info;
    }

    int offset = headerEnd;
    bool sawChunk = false;
    bool allFull = true;
    int lastSize = -1;

    while (offset + 16 <= data.size()) {
        quint32 magic = readLe32(data, offset);
        if (magic != kMagic) {
            break;
        }
        quint32 compressedSize = readLe32(data, offset + 4);
        quint32 uncompressedSize = readLe32(data, offset + 8);
        offset += 16;

        if (compressedSize == 0 && uncompressedSize == 0) {
            info.hasTerminalChunk = true;
            break;
        }
        if (compressedSize == 0 || uncompressedSize == 0) {
            break;
        }
        if (!sawChunk) {
            info.blockSize = static_cast<int>(uncompressedSize);
            sawChunk = true;
        }
        if (static_cast<int>(uncompressedSize) != info.blockSize) {
            allFull = false;
        }
        lastSize = static_cast<int>(uncompressedSize);

        offset += static_cast<int>(compressedSize);
        if (offset > data.size()) {
            break;
        }
    }

    if (sawChunk) {
        info.padToBlock = allFull && lastSize == info.blockSize;
    }
    return info;
}

QByteArray decodeRawPayload(const QByteArray &data)
{
    int offset = findHeaderEnd(data);
    if (offset < 0) {
        return QByteArray();
    }
    QByteArray output;
    const qint64 dataSize = data.size();
    while (offset + 16 <= dataSize) {
        quint32 magic = readLe32(data, offset);
        if (magic != kMagic) {
            break;
        }
        quint32 compressedSize = readLe32(data, offset + 4);
        quint32 uncompressedSize = readLe32(data, offset + 8);
        offset += 16;

        if (compressedSize == 0 && uncompressedSize == 0) {
            break;
        }
        if (compressedSize == 0 || uncompressedSize == 0) {
            break;
        }
        if (offset + static_cast<qint64>(compressedSize) > dataSize) {
            break;
        }

        QByteArray decompressed;
        decompressed.resize(static_cast<int>(uncompressedSize));
        int decoded = LZ4_decompress_safe(data.constData() + offset, decompressed.data(),
                                          static_cast<int>(compressedSize),
                                          static_cast<int>(uncompressedSize));
        if (decoded < 0) {
            return QByteArray();
        }
        output.append(decompressed.left(decoded));
        offset += static_cast<qint64>(compressedSize);
    }
    return output;
}

int countTrailingNulls(const QByteArray &data)
{
    int count = 0;
    for (int i = data.size() - 1; i >= 0; --i) {
        if (data.at(i) != '\0') {
            break;
        }
        count++;
    }
    return count;
}

int detectTrailingNulls(const QByteArray &data)
{
    QByteArray output = decodeRawPayload(data);
    if (output.isEmpty()) {
        return 0;
    }
    int lastObject = output.lastIndexOf('}');
    int lastArray = output.lastIndexOf(']');
    int lastGood = qMax(lastObject, lastArray);
    if (lastGood < 0 || lastGood + 1 >= output.size()) {
        return 0;
    }
    QByteArray tail = output.mid(lastGood + 1);
    return countTrailingNulls(tail);
}

bool debugSaveEnabled()
{
    return qEnvironmentVariableIntValue("NMSSE_DEBUG_SAVE") == 1;
}

void logSaveFormat(const QString &path, const BlockFormatInfo &info)
{
    if (!debugSaveEnabled()) {
        return;
    }
    QFileInfo infoPath(path);
    QString canonical = infoPath.canonicalFilePath();
    qInfo() << "SaveEncoder path" << path << "canonical"
            << (canonical.isEmpty() ? infoPath.absoluteFilePath() : canonical);
    qInfo() << "SaveEncoder format for" << path << "blockSize=" << info.blockSize
            << "padToBlock=" << info.padToBlock << "hasTerminalChunk=" << info.hasTerminalChunk
            << "trailingNulls=" << info.trailingNulls;
}

void logWrittenFileSummary(const QString &path)
{
    if (!debugSaveEnabled()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qInfo() << "SaveEncoder post-write: unable to read" << path;
        return;
    }
    QByteArray data = file.readAll();
    file.close();

    qint64 offset = -1;
    for (int i = 0; i + 4 <= data.size(); ++i) {
        if (readLe32(data, i) == kMagic) {
            offset = i;
            break;
        }
    }
    if (offset < 0) {
        qInfo() << "SaveEncoder post-write: magic not found in" << path;
        return;
    }

    int chunkCount = 0;
    quint32 firstSize = 0;
    quint32 lastSize = 0;
    bool terminal = false;
    qint64 cursor = offset;
    while (cursor + 16 <= data.size()) {
        quint32 magic = readLe32(data, static_cast<int>(cursor));
        if (magic != kMagic) {
            break;
        }
        quint32 compressedSize = readLe32(data, static_cast<int>(cursor + 4));
        quint32 uncompressedSize = readLe32(data, static_cast<int>(cursor + 8));
        cursor += 16;
        if (compressedSize == 0 && uncompressedSize == 0) {
            terminal = true;
            break;
        }
        if (compressedSize == 0 || uncompressedSize == 0) {
            break;
        }
        if (chunkCount == 0) {
            firstSize = uncompressedSize;
        }
        lastSize = uncompressedSize;
        chunkCount++;
        cursor += static_cast<qint64>(compressedSize);
    }

    qInfo() << "SaveEncoder post-write:" << path << "size=" << data.size()
            << "chunks=" << chunkCount << "firstSize=" << firstSize
            << "lastSize=" << lastSize << "terminal=" << terminal;
}

int slotIndexForSaveName(const QString &fileName)
{
    QRegularExpression numRegex("save(\\d+)\\.hg");
    QRegularExpressionMatch match = numRegex.match(fileName);
    if (match.hasMatch()) {
        return match.captured(1).toInt() - 1;
    }
    return 0;
}
}

bool SaveEncoder::encodeSave(const QString &filePath, const QJsonObject &saveData, QString *errorMessage)
{
    QJsonDocument doc(saveData);
    QByteArray json = doc.toJson(QJsonDocument::Compact);
    return encodeSave(filePath, json, errorMessage);
}

bool SaveEncoder::encodeSave(const QString &filePath, const QByteArray &json, QString *errorMessage)
{
    QFile original(filePath);
    if (!original.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = QString("Unable to read %1").arg(filePath);
        }
        return false;
    }
    QByteArray originalBytes = original.readAll();
    original.close();

    int headerEnd = findHeaderEnd(originalBytes);
    QByteArray header;
    BlockFormatInfo formatInfo = detectBlockFormat(originalBytes, headerEnd < 0 ? 0 : headerEnd);
    formatInfo.trailingNulls = detectTrailingNulls(originalBytes);
    int blockSize = formatInfo.blockSize;
    logSaveFormat(filePath, formatInfo);
    if (headerEnd >= 0) {
        header = originalBytes.left(headerEnd);
    } else {
        header.clear();
    }

    QByteArray payload = json;
    if (formatInfo.trailingNulls > 0) {
        payload.append(QByteArray(formatInfo.trailingNulls, '\0'));
    }

    QFile out(filePath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = QString("Unable to write %1").arg(filePath);
        }
        return false;
    }

    if (!header.isEmpty()) {
        out.write(header);
    }

    int offset = 0;
    while (offset < payload.size()) {
        int chunkSize = qMin(blockSize, payload.size() - offset);

        QByteArray block;
        int uncompressedSize = chunkSize;
        if (formatInfo.padToBlock && chunkSize < blockSize) {
            block = QByteArray(blockSize, '\0');
            memcpy(block.data(), payload.constData() + offset, chunkSize);
            uncompressedSize = blockSize;
        } else {
            block = payload.mid(offset, chunkSize);
        }

        int maxCompressedSize = LZ4_compressBound(uncompressedSize);
        QByteArray compressed;
        compressed.resize(maxCompressedSize);

        int compressedSize = LZ4_compress_default(block.constData(), compressed.data(),
                                                   uncompressedSize, maxCompressedSize);
        if (compressedSize <= 0) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("LZ4 compression failed");
            }
            return false;
        }
        compressed.truncate(compressedSize);

        out.write(intToLe32(kMagic));
        out.write(intToLe32(static_cast<quint32>(compressedSize)));
        out.write(intToLe32(static_cast<quint32>(uncompressedSize)));
        out.write(intToLe32(0));
        out.write(compressed);

        offset += chunkSize;
    }

    if (formatInfo.hasTerminalChunk) {
        // EOF sentinel — exactly 16 bytes (Magic + 12 zeros)
        out.write(intToLe32(kMagic));
        out.write(QByteArray(12, '\0'));
    }

    out.flush();
    out.close();
    logWrittenFileSummary(filePath);

    QFileInfo saveInfo(filePath);
    QString mfName = saveInfo.fileName().replace("save", "mf_save");
    QString mfPath = QDir(saveInfo.absolutePath()).filePath(mfName);
    int slotIdx = slotIndexForSaveName(saveInfo.fileName());

    if (qEnvironmentVariableIntValue("NMSSE_SKIP_MANIFEST") != 1 && QFile::exists(mfPath)) {
        QFile finalSave(filePath);
        if (finalSave.open(QIODevice::ReadOnly)) {
            QByteArray finalBytes = finalSave.readAll();
            finalSave.close();
            ManifestManager::writeManifest(mfPath, slotIdx, finalBytes, ManifestData());
        }
    }

    if (QFile::exists(mfPath) && (debugSaveEnabled() || qEnvironmentVariableIntValue("NMSSE_DEBUG_MANIFEST") == 1)) {
        QFile finalSave(filePath);
        if (finalSave.open(QIODevice::ReadOnly)) {
            QByteArray finalBytes = finalSave.readAll();
            finalSave.close();
            ManifestManager::logManifestValidation(mfPath, slotIdx, finalBytes);
        }
    }

    return true;
}
