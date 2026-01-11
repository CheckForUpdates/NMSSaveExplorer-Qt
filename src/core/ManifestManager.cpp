#include "core/ManifestManager.h"
#include "core/XXTEA.h"
#include <QCryptographicHash>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <cstring>

#include "core/SpookyHash.h"

namespace {
uint32_t rotateLeft(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

bool debugManifestEnabled()
{
    return qEnvironmentVariableIntValue("NMSSE_DEBUG_MANIFEST") == 1
        || qEnvironmentVariableIntValue("NMSSE_DEBUG_SAVE") == 1;
}

int scoreLocationCandidate(const QString &text)
{
    if (text.isEmpty()) {
        return -1;
    }
    int score = 0;
    QChar first = text.at(0);
    if (first.isUpper()) {
        score += 10;
    }
    if (first.isLetterOrNumber()) {
        score += 5;
    }
    score += qMin(text.size(), 60);
    return score;
}

QString decodeLocationCandidate(const char *data, int byteOffset, int totalBytes)
{
    if (byteOffset < 0 || byteOffset + 64 > totalBytes) {
        return QString();
    }
    QByteArray bytes(data + byteOffset, 64);
    
    // Find first null or non-printable character to truncate
    int actualSize = 0;
    while (actualSize < bytes.size()) {
        unsigned char c = static_cast<unsigned char>(bytes[actualSize]);
        if (c < 32 || c == 127) {
            break;
        }
        actualSize++;
    }
    bytes.truncate(actualSize);

    QString text = QString::fromUtf8(bytes).trimmed();
    int leading = 0;
    while (leading < text.size() && !text.at(leading).isLetterOrNumber()) {
        leading++;
    }
    if (leading > 0) {
        text = text.mid(leading).trimmed();
    }
    return text;
}

QString decodeLocationName(const uint32_t *words, int wordCount)
{
    const char *data = reinterpret_cast<const char *>(words);
    const int totalBytes = wordCount * 4;
    const int start = 120;
    const int end = 220;
    QString best;
    int bestScore = -1;
    for (int offset = start; offset <= end; ++offset) {
        QString candidate = decodeLocationCandidate(data, offset, totalBytes);
        int score = scoreLocationCandidate(candidate);
        if (score > bestScore) {
            bestScore = score;
            best = candidate;
        }
    }
    return best;
}

QByteArray computeSpooky(const QByteArray &saveBytes, const QByteArray &sha256)
{
    uint64_t sh1 = 0x0155af93ac304200ULL;
    uint64_t sh2 = 0x8ac7230489e7ffffULL;
    SpookyHash::Hash128(sha256.constData(), sha256.size(), &sh1, &sh2);
    SpookyHash::Hash128(saveBytes.constData(), saveBytes.size(), &sh1, &sh2);

    QByteArray spooky;
    spooky.resize(16);
    std::memcpy(spooky.data(), &sh1, 8);
    std::memcpy(spooky.data() + 8, &sh2, 8);
    return spooky;
}
}

void ManifestManager::deriveKey(int slotIndex, uint32_t key[4]) {
    uint32_t internalArchiveNumber = slotIndex + 2;
    uint32_t k0 = internalArchiveNumber ^ 0x1422cb8c;
    uint32_t h1 = rotateLeft(k0, 13) * 5 + 0xe6546b64;
    
    std::memcpy(key, "NAESEVADNAYRTNRG", 16);
    uint8_t* kb = reinterpret_cast<uint8_t*>(key);
    kb[0] = static_cast<uint8_t>(h1 & 0xFF);
    kb[1] = static_cast<uint8_t>((h1 >> 8) & 0xFF);
    kb[2] = static_cast<uint8_t>((h1 >> 16) & 0xFF);
    kb[3] = static_cast<uint8_t>((h1 >> 24) & 0xFF);
}

ManifestData ManifestManager::readManifest(const QString &path, int slotIndex) {
    ManifestData data;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return data;
    
    QByteArray bytes = file.readAll();
    file.close();
    
    if (bytes.size() < 432 || bytes.size() % 4 != 0) return data;
    
    uint32_t key[4];
    deriveKey(slotIndex, key);
    
    uint32_t *words = reinterpret_cast<uint32_t*>(bytes.data());
    XXTEA::decrypt(words, bytes.size() / 4, key);
    
    data.version = words[0];
    if (data.version != 0xEEEEEEBE) return data;
    
    // Extract SHA256 (Words 6-13, 32 bytes)
    data.sha256 = QByteArray(reinterpret_cast<const char*>(&words[6]), 32);
    
    // Extract SpookyHash V2 (Words 2-5, 16 bytes)
    data.spooky = QByteArray(reinterpret_cast<const char*>(&words[2]), 16);

    // Extract Location Name (heuristic offset; handles layout variations)
    const int wordCount = bytes.size() / 4;
    data.locationName = decodeLocationName(words, wordCount);
    if (qEnvironmentVariableIsSet("NMSSE_DEBUG_LOCATION")) {
        qInfo() << "Manifest location candidates for" << path;
        const char *data = reinterpret_cast<const char *>(words);
        const int totalBytes = wordCount * 4;
        for (int offset = 120; offset <= 220; ++offset) {
            QString candidate = decodeLocationCandidate(data, offset, totalBytes);
            if (candidate.size() >= 4) {
                qInfo() << "  byte" << offset << ":" << candidate;
            }
        }
    }
    
    uint64_t *timePtr = reinterpret_cast<uint64_t*>(&words[14]);
    data.lastSaveTime = *timePtr;

    return data;
}

bool ManifestManager::writeManifest(const QString &path, int slotIndex, const QByteArray &saveBytes, const ManifestData &baseData) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QByteArray bytes = file.readAll();
    file.close();

    if (bytes.size() < 432) return false;

    uint32_t key[4];
    deriveKey(slotIndex, key);

    uint32_t *words = reinterpret_cast<uint32_t*>(bytes.data());
    XXTEA::decrypt(words, bytes.size() / 4, key);

    if (words[0] != 0xEEEEEEBE) return false;

    // Calculate new SHA256
    QByteArray newSha = QCryptographicHash::hash(saveBytes, QCryptographicHash::Sha256);
    std::memcpy(&words[6], newSha.constData(), 32);

    // Calculate new SpookyHash V2
    // Seed 0: 0x0155af93ac304200
    // Seed 1: 0x8ac7230489e7ffff
    uint64_t h1 = 0x0155af93ac304200ULL;
    uint64_t h2 = 0x8ac7230489e7ffffULL;
    
    // Mixed hash calculation: 
    // 1. Spooky(SHA256 of save) -> h1, h2 seeds
    // 2. Spooky(Raw save data) using h1, h2 as seeds
    uint64_t sh1 = h1, sh2 = h2;
    SpookyHash::Hash128(newSha.constData(), newSha.size(), &sh1, &sh2);
    SpookyHash::Hash128(saveBytes.constData(), saveBytes.size(), &sh1, &sh2);
    
    std::memcpy(&words[2], &sh1, 8);
    std::memcpy(&words[4], &sh2, 8); 

    XXTEA::encrypt(words, bytes.size() / 4, key);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    file.write(bytes);
    file.close();

    return true;
}

void ManifestManager::logManifestValidation(const QString &path, int slotIndex, const QByteArray &saveBytes)
{
    if (!debugManifestEnabled()) {
        return;
    }
    QFileInfo info(path);
    if (!info.exists()) {
        qInfo() << "Manifest validation: missing manifest at" << path;
        return;
    }

    ManifestData manifest = readManifest(path, slotIndex);
    if (!manifest.isValid()) {
        qInfo() << "Manifest validation: invalid manifest for" << path << "slot" << slotIndex;
        return;
    }

    QByteArray computedSha = QCryptographicHash::hash(saveBytes, QCryptographicHash::Sha256);
    QByteArray computedSpooky = computeSpooky(saveBytes, computedSha);
    bool shaMatch = (manifest.sha256 == computedSha);
    bool spookyMatch = (manifest.spooky == computedSpooky);

    qInfo() << "Manifest validation for" << path << "slot" << slotIndex
            << "shaMatch=" << shaMatch << "spookyMatch=" << spookyMatch
            << "saveSize=" << saveBytes.size();
    if (!shaMatch) {
        qInfo() << "  manifest sha=" << manifest.sha256.toHex()
                << "computed sha=" << computedSha.toHex();
    }
    if (!spookyMatch) {
        qInfo() << "  manifest spooky=" << manifest.spooky.toHex()
                << "computed spooky=" << computedSpooky.toHex();
    }
}
