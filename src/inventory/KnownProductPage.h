#pragma once

#include <QJsonDocument>
#include <QVariant>
#include <QWidget>
#include <memory>

#include "core/LosslessJsonDocument.h"

class KnownProductDialog;

class KnownProductPage : public QWidget
{
    Q_OBJECT

public:
    explicit KnownProductPage(QWidget *parent = nullptr);

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
    void updateActiveContext();
    QVariantList playerBasePath() const;
    void applyValueAtPath(const QVariantList &path, const QJsonValue &value);
    bool syncRootFromLossless(QString *errorMessage = nullptr);
    void resetEditor(const QJsonArray &knownProducts, const QVariantList &knownPath);

    KnownProductDialog *editor_ = nullptr;
    QJsonDocument rootDoc_;
    std::shared_ptr<LosslessJsonDocument> losslessDoc_;
    QString currentFilePath_;
    bool hasUnsavedChanges_ = false;
    bool usingExpeditionContext_ = false;
};
