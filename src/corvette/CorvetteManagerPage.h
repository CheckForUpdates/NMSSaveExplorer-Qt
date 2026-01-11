#pragma once

#include <QJsonDocument>
#include <QJsonValue>
#include <QVariantList>
#include <QWidget>
#include <memory>

class QComboBox;
class QPushButton;
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
    bool saveChanges(QString *errorMessage);
    bool hasLoadedSave() const { return !currentFilePath_.isEmpty(); }
    bool hasUnsavedChanges() const { return hasUnsavedChanges_; }
    const QString &currentFilePath() const { return currentFilePath_; }

signals:
    void statusMessage(const QString &message);

private slots:
    void onImportClicked();
    void onExportClicked();
    void onUseClicked();
    void onCorvetteSelected(int index);

private:
    void buildUi();
    void rebuildCorvetteList();
    void refreshGrids();
    void loadLocalCorvettes();
    QString localCorvettesPath() const;
    
    QJsonObject activePlayerState() const;
    QVariantList corvetteInventoryPath() const;
    QVariantList corvetteLayoutPath() const;
    QVariantList playerBasePath() const;
    QVariantList playerStatePathForContext(bool expedition) const;
    void updateActiveContext();
    bool playerHasCorvetteData(bool expedition) const;
    
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

    QList<CorvetteEntry> localCorvettes_;
    QJsonDocument rootDoc_;
    std::shared_ptr<LosslessJsonDocument> losslessDoc_;
    QString currentFilePath_;
    bool hasUnsavedChanges_ = false;
    bool usingExpeditionContext_ = false;
};
