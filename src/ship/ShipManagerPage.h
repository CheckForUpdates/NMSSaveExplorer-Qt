#pragma once

#include <QJsonDocument>
#include <QVariantList>
#include <QWidget>
#include <functional>
#include <memory>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;
class QScrollArea;
class LosslessJsonDocument;

struct ShipEntry {
    int index = -1;
    QString name;
};

class ShipManagerPage : public QWidget
{
    Q_OBJECT

public:
    explicit ShipManagerPage(QWidget *parent = nullptr);
    bool loadFromFile(const QString &filePath, QString *errorMessage);
    bool loadFromPrepared(const QString &filePath, const QJsonDocument &doc,
                          const std::shared_ptr<LosslessJsonDocument> &losslessDoc,
                          QString *errorMessage = nullptr);
    bool saveChanges(QString *errorMessage);
    bool hasLoadedSave() const;
    bool hasUnsavedChanges() const;
    const QString &currentFilePath() const;
    void clearLoadedSave();

signals:
    void statusMessage(const QString &message);

private:
    void buildUi();
    void updateActiveContext();
    void rebuildShipList();
    void setActiveShip(int index);
    void importShip();
    void exportShip();

    QJsonObject activePlayerState() const;
    QJsonArray shipOwnershipArray() const;
    QVariantList shipOwnershipPath() const;
    QVariantList shipOwnershipPathForContext(bool expedition) const;
    QVariantList playerStatePathForContext(bool expedition) const;
    QVariantList contextRootPathForContext(bool expedition) const;
    void updateShipAtIndex(int index, const std::function<void(QJsonObject &)> &mutator);
    void updateShipAtIndexOnPath(const QVariantList &path, int index,
                                 const std::function<void(QJsonObject &)> &mutator, bool updateUi);
    void updateShipResource(QJsonObject &ship, const std::function<void(QJsonObject &)> &mutator);
    void updateShipInventoryClass(QJsonObject &ship, const QString &value);
    void updatePlayerShipResources(const QJsonObject &oldResource, const QJsonObject &newResource);
    bool updatePlayerStateResourceAtPath(const QVariantList &path, const QJsonObject &oldResource,
                                         const QJsonObject &newResource);
    void updateContextResources(const QVariantList &contextPath, const QJsonObject &oldResource,
                                const QJsonObject &newResource);

    QJsonValue valueAtPath(const QJsonValue &root, const QVariantList &path) const;
    QJsonValue setValueAtPath(const QJsonValue &root, const QVariantList &path, int depth,
                              const QJsonValue &value) const;
    void applyValueAtPath(const QVariantList &path, const QJsonValue &value);

    void refreshShipFields(const QJsonObject &ship);
    QString shipNameFromObject(const QJsonObject &ship) const;
    QString shipClassFromObject(const QJsonObject &ship) const;
    QString shipSeedFromObject(const QJsonObject &ship) const;
    QString shipTypeFromObject(const QJsonObject &ship) const;
    bool shipUseLegacyColours(const QJsonObject &ship) const;
    double shipStatValue(const QJsonObject &ship, const QString &statId) const;
    QJsonObject inventoryObjectForShip(const QJsonObject &ship) const;
    QString formatNumber(double value) const;
    QString formattedSeed(qulonglong seed) const;
    bool syncRootFromLossless(QString *errorMessage = nullptr);

    QScrollArea *scrollArea_ = nullptr;
    QWidget *formWidget_ = nullptr;
    QComboBox *shipCombo_ = nullptr;
    QLineEdit *nameField_ = nullptr;
    QComboBox *typeCombo_ = nullptr;
    QComboBox *classCombo_ = nullptr;
    QLineEdit *seedField_ = nullptr;
    QCheckBox *useOldColours_ = nullptr;
    QPushButton *importButton_ = nullptr;
    QPushButton *exportButton_ = nullptr;

    QLineEdit *healthField_ = nullptr;
    QLineEdit *shieldField_ = nullptr;
    QLineEdit *damageField_ = nullptr;
    QLineEdit *shieldsField_ = nullptr;
    QLineEdit *hyperdriveField_ = nullptr;
    QLineEdit *maneuverField_ = nullptr;

    QList<ShipEntry> ships_;
    int activeShipIndex_ = -1;
    bool usingExpeditionContext_ = false;
    QJsonDocument rootDoc_;
    std::shared_ptr<LosslessJsonDocument> losslessDoc_;
    QString currentFilePath_;
    bool hasUnsavedChanges_ = false;
};
