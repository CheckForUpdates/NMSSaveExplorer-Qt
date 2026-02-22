#pragma once

#include <QJsonDocument>
#include <QJsonValue>
#include <QVariant>
#include <QWidget>
#include <memory>

#include "core/LosslessJsonDocument.h"

class QTabWidget;

class InventoryEditorPage : public QWidget
{
    Q_OBJECT

public:
    enum class InventorySection {
        Inventories = 0x1,
        Currencies = 0x2,
        Expedition = 0x4,
        Settlement = 0x8,
        StorageManager = 0x10
    };
    Q_DECLARE_FLAGS(InventorySections, InventorySection)

    explicit InventoryEditorPage(
        QWidget *parent = nullptr,
        InventorySections sections = InventorySections(
            static_cast<int>(InventorySection::Inventories)
            | static_cast<int>(InventorySection::Currencies)
            | static_cast<int>(InventorySection::Expedition)
            | static_cast<int>(InventorySection::Settlement)
            | static_cast<int>(InventorySection::StorageManager)));

    bool loadFromFile(const QString &filePath, QString *errorMessage = nullptr);
    bool loadFromPrepared(const QString &filePath, const QJsonDocument &doc,
                          const std::shared_ptr<LosslessJsonDocument> &losslessDoc,
                          QString *errorMessage = nullptr);
    bool hasLoadedSave() const;
    bool hasUnsavedChanges() const;
    const QString &currentFilePath() const;
    bool saveChanges(QString *errorMessage = nullptr);
    void clearLoadedSave();
    void setShowIds(bool show);
    bool showIds() const { return showIds_; }
    
    static QJsonValue valueAtPath(const QJsonValue &root, const QVariantList &path);
    static QJsonValue setValueAtPath(const QJsonValue &root, const QVariantList &path, int depth,
                              const QJsonValue &value);

signals:
    void statusMessage(const QString &message);

private:
    enum class InventoryType {
        Other,
        Ship,
        Multitool,
        Vehicle
    };

    struct InventoryDescriptor {
        QString name;
        QVariantList slotsPath;
        QVariantList validPath;
        QVariantList specialSlotsPath;
        InventoryType type = InventoryType::Other;
    };

    void rebuildTabs();
    void updateActiveContext();
    QVariantList playerBasePath() const;

    bool resolveExosuit(InventoryDescriptor &out) const;
    bool resolveExosuitTech(InventoryDescriptor &out) const;
    bool resolveShip(InventoryDescriptor &out) const;
    bool resolveShipTech(InventoryDescriptor &out) const;
    bool resolveMultitool(InventoryDescriptor &out) const;
    bool resolveMultitoolTech(InventoryDescriptor &out) const;
    bool resolveVehicle(InventoryDescriptor &out) const;
    bool resolveVehicleTech(InventoryDescriptor &out) const;
    bool resolveFreighter(InventoryDescriptor &out) const;
    bool resolveFrigateCache(InventoryDescriptor &out) const;
    void addCurrenciesTab();
    void addExpeditionTab();
    void addSettlementTab();
    void addStorageManagerTab();
    void applyValueAtPath(const QVariantList &path, const QJsonValue &value, bool deferSync = false);
    void applyDiffAtPath(const QVariantList &path, const QJsonValue &current, const QJsonValue &updated);
    void finalizeDeferredSync();

    QWidget *buildCurrencyRow(const QString &labelText, const QString &jsonKey,
                              const QString &iconId, const QVariantList &playerPath,
                              QJsonObject &playerState);
    QWidget *buildExpeditionStage(const QJsonObject &stage, const QJsonArray &milestones,
                                  QJsonArray &milestoneValues, const QVariantList &milestonePath,
                                  int stageIndex, int milestoneStart, bool showCompleteAll);
    QWidget *buildSettlementForm(QJsonObject &settlement);
    QWidget *buildStorageManager();

    QString formatStatId(const QString &id) const;
    QString formatExpeditionToken(const QString &raw) const;
    bool syncRootFromLossless(QString *errorMessage = nullptr);
    QString formatQuantity(double value) const;
    bool isDecryptMissionUnlocked(const QJsonObject &encryption) const;
    void updateDecryptMissionProgress(const QJsonObject &encryption, int progressValue);
    void forceDecryptMilestone(int stageIndex, int milestoneIndex);
    QJsonObject activePlayerState() const;
    QJsonArray ensureMilestoneArray(QJsonObject &seasonState, int requiredSize) const;
    QJsonObject settlementRoot() const;


    QTabWidget *tabs_ = nullptr;
    QJsonDocument rootDoc_;
    std::shared_ptr<LosslessJsonDocument> losslessDoc_;
    QString currentFilePath_;
    bool hasUnsavedChanges_ = false;
    bool usingExpeditionContext_ = false;
    InventorySections sections_;
    bool showIds_ = false;

    int selectedShipIndex_ = 0;
    int selectedMultitoolIndex_ = 0;
    int selectedVehicleIndex_ = 0;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(InventoryEditorPage::InventorySections)
