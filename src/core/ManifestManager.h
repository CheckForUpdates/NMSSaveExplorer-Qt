#pragma once

#include <QString>
#include <QByteArray>
#include <cstdint>

struct ManifestData {
    uint32_t version = 0;
    QByteArray sha256;
    QByteArray spooky;
    QString locationName;
    QString playerID; // Steam ID usually
    uint64_t lastSaveTime = 0;
    
    bool isValid() const { return version == 0xEEEEEEBE; }
};

class ManifestManager {
public:
    static ManifestData readManifest(const QString &path, int slotIndex);
    static bool writeManifest(const QString &path, int slotIndex, const QByteArray &saveBytes, const ManifestData &baseData);
    static void logManifestValidation(const QString &path, int slotIndex, const QByteArray &saveBytes);

private:
    static void deriveKey(int slotIndex, uint32_t key[4]);
};
