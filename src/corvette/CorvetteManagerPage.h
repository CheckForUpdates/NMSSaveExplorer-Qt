#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QVariantList>
#include <QWidget>
#include <functional>
#include <memory>

class QComboBox;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QScrollArea;
class InventoryGridWidget;
class LosslessJsonDocument;

struct CorvetteEntry {
    QString fileName;
    QString name;
    QJsonValue seed;
    bool inUse = false;
};

class CorvetteManagerPage : public QWidget
{
    Q_OBJECT

public:
    explicit CorvetteManagerPage(QWidget *parent = nullptr);
    bool loadFromFile(const QString &filePath, QString *errorMessage);
    bool loadFromPrepared(const QString &filePath, const QJsonDocument &doc,
                          const std::shared_ptr<LosslessJsonDocument> &losslessDoc,
                          QString *errorMessage = nullptr);
    bool saveChanges(QString *errorMessage);
    bool hasLoadedSave() const { return !currentFilePath_.isEmpty(); }
    bool hasUnsavedChanges() const { return hasUnsavedChanges_; }
    const QString &currentFilePath() const { return currentFilePath_; }
    void clearLoadedSave();

signals:
    void statusMessage(const QString &message);

private slots:
    void onImportClicked();
    void onExportClicked();
    void onUseClicked();
    void onCorvetteSelected(int index);
    void onFrigateSelected(int index);
    void onFrigateFieldEdited();

private:
    void buildUi();
    void rebuildCorvetteList();
    void rebuildFrigateList();
    void refreshGrids();
    void refreshFrigateEditor();
    void refreshFrigateProgressFields(const QJsonObject &frigate);
    void loadLocalCorvettes();
    QString localCorvettesPath() const;
    
    QJsonObject activePlayerState() const;
    QVariantList corvetteInventoryPath() const;
    QVariantList corvetteLayoutPath() const;
    QVariantList fleetFrigatesPath() const;
    QVariantList fleetExpeditionsPath() const;
    QVariantList playerBasePath() const;
    QVariantList playerStatePathForContext(bool expedition) const;
    void updateActiveContext();
    bool playerHasCorvetteData(bool expedition) const;
    bool playerHasFrigateData(bool expedition) const;
    bool frigateIsOnMission(int frigateIndex) const;
    void updateFrigateAtIndex(int frigateIndex, const std::function<void(QJsonObject &)> &mutator);
    
    QJsonValue valueAtPath(const QJsonValue &root, const QVariantList &path) const;
    void applyValueAtPath(const QVariantList &path, const QJsonValue &value);
    bool syncRootFromLossless(QString *errorMessage = nullptr);

    QComboBox *corvetteCombo_ = nullptr;
    QPushButton *importButton_ = nullptr;
    QPushButton *exportButton_ = nullptr;
    QPushButton *useButton_ = nullptr;
    
    QTabWidget *tabs_ = nullptr;
    InventoryGridWidget *inventoryGrid_ = nullptr;
    InventoryGridWidget *techGrid_ = nullptr;
    QComboBox *frigateCombo_ = nullptr;
    QLineEdit *frigateNameEdit_ = nullptr;
    QComboBox *frigateClassCombo_ = nullptr;
    QComboBox *frigateInventoryClassCombo_ = nullptr;
    QLineEdit *frigateHomeSeedEdit_ = nullptr;
    QLineEdit *frigateResourceSeedEdit_ = nullptr;
    QComboBox *frigateRaceCombo_ = nullptr;
    QList<QSpinBox *> frigateStatSpins_;
    QList<QComboBox *> frigateTraitCombos_;
    QSpinBox *frigateTotalExpSpin_ = nullptr;
    QSpinBox *frigateTimesDamagedSpin_ = nullptr;
    QSpinBox *frigateSuccessSpin_ = nullptr;
    QSpinBox *frigateFailedSpin_ = nullptr;
    QLineEdit *frigateLevelUpInEdit_ = nullptr;
    QLineEdit *frigateLevelUpsRemainingEdit_ = nullptr;
    QLineEdit *frigateMissionStateEdit_ = nullptr;
    bool updatingFrigateUi_ = false;

    QList<CorvetteEntry> localCorvettes_;
    QJsonDocument rootDoc_;
    std::shared_ptr<LosslessJsonDocument> losslessDoc_;
    QString currentFilePath_;
    bool hasUnsavedChanges_ = false;
    bool usingExpeditionContext_ = false;
};
