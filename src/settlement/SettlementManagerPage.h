#pragma once

#include <QJsonDocument>
#include <QSet>
#include <QStringList>
#include <QVariant>
#include <QWidget>
#include <memory>

#include "core/LosslessJsonDocument.h"

class QComboBox;
class QScrollArea;

class SettlementManagerPage : public QWidget
{
    Q_OBJECT

public:
    explicit SettlementManagerPage(QWidget *parent = nullptr);

    bool loadFromFile(const QString &filePath, QString *errorMessage = nullptr);
    bool loadFromPrepared(const QString &filePath, const QJsonDocument &doc,
                          const std::shared_ptr<LosslessJsonDocument> &losslessDoc,
                          QString *errorMessage = nullptr);
    bool hasLoadedSave() const;
    bool hasUnsavedChanges() const;
    const QString &currentFilePath() const;
    bool saveChanges(QString *errorMessage = nullptr);
    void clearLoadedSave();

signals:
    void statusMessage(const QString &message);

private:
    struct SettlementEntry {
        int index = -1;
        QString name;
    };

    void buildUi();
    void rebuildSettlementList();
    void setActiveSettlement(int index);
    QWidget *buildSettlementForm(int index);
    void resolveSettlementStatesPath();

    void updateActiveContext();
    QVariantList playerBasePath() const;
    QVariantList settlementStatesPath() const;
    QVariantList findSettlementStatesPath(const QJsonValue &value, const QVariantList &path) const;
    QList<SettlementEntry> collectOwnedSettlements() const;
    void collectPlayerOwnerIds(QSet<QString> &lids, QSet<QString> &uids, QSet<QString> &usns) const;
    QString stringForKeys(const QJsonObject &obj, const QStringList &keys) const;
    QJsonObject objectForKeys(const QJsonObject &obj, const QStringList &keys) const;
    QString resolveUsername() const;
    QJsonObject settlementAtIndex(int index) const;

    QJsonValue valueAtPath(const QJsonValue &root, const QVariantList &path) const;
    QJsonValue setValueAtPath(const QJsonValue &root, const QVariantList &path, int depth,
                              const QJsonValue &value) const;
    void applyValueAtPath(const QVariantList &path, const QJsonValue &value);
    bool syncRootFromLossless(QString *errorMessage = nullptr);

    QComboBox *settlementCombo_ = nullptr;
    QScrollArea *scrollArea_ = nullptr;
    QWidget *formWidget_ = nullptr;

    QList<SettlementEntry> settlements_;
    QVariantList settlementStatesPath_;
    QJsonDocument rootDoc_;
    std::shared_ptr<LosslessJsonDocument> losslessDoc_;
    QString currentFilePath_;
    bool hasUnsavedChanges_ = false;
    bool usingExpeditionContext_ = false;
};
