#pragma once

#include <QHash>
#include <QJsonDocument>
#include <QSet>
#include <QVariant>
#include <QWidget>
#include <memory>

#include "core/LosslessJsonDocument.h"

class QPlainTextEdit;
class QPushButton;
class QStandardItem;
class QStandardItemModel;
class QTreeView;
class QLineEdit;

class JsonExplorerPage : public QWidget
{
    Q_OBJECT

public:
    explicit JsonExplorerPage(QWidget *parent = nullptr);

    bool loadFromFile(const QString &filePath, QString *errorMessage = nullptr);
    void setRootDoc(const QJsonDocument &doc, const QString &filePath,
                    const std::shared_ptr<LosslessJsonDocument> &losslessDoc);
    bool hasLoadedSave() const;
    bool saveChanges(QString *errorMessage = nullptr);
    bool saveAs(const QString &filePath, QString *errorMessage = nullptr);
    bool exportJson(const QString &filePath, QString *errorMessage = nullptr) const;
    bool hasUnsavedChanges() const;
    void clearLoadedSave();

    void expandAll();
    void collapseAll();

signals:
    void statusMessage(const QString &message);

private:
    void buildTree();
    void populateChildren(QStandardItem *parent);
    void addPlaceholderIfNeeded(QStandardItem *item, const QJsonValue &value);
    const rapidjson::Value *losslessValueAtPath(const QVariantList &path) const;
    QJsonValue valueAtPath(const QVariantList &path) const;
    QJsonValue mapToReadable(const QJsonValue &value) const;
    QJsonValue remapToShort(const QJsonValue &value) const;
    QJsonValue setValueAtPath(const QJsonValue &root, const QVariantList &path, int depth, const QJsonValue &value) const;
    QString pathKey(const QVariantList &path) const;
    QString displayPath(QStandardItem *item) const;

    void loadEditorForItem(QStandardItem *item);
    bool commitEditor();
    QString prettyPrinted(const QJsonValue &value) const;
    void markModified(QStandardItem *item);
    void clearModified(QStandardItem *item);

    void showFindDialog();
    void performFind(const QString &text, bool backward, bool wrap, bool caseSensitive, bool wholeWord, bool useRegex);
    void performTreeSearch(bool backward);
    bool itemMatchesTreeSearch(QStandardItem *item, const QString &needle) const;
    void collectTreeItems(QStandardItem *item, QList<QStandardItem *> &outItems);

    void ensureMappingLoaded();
    bool syncRootFromLossless(QString *errorMessage = nullptr);

    QTreeView *tree_ = nullptr;
    QLineEdit *treeSearchField_ = nullptr;
    QPushButton *treeSearchPrevButton_ = nullptr;
    QPushButton *treeSearchNextButton_ = nullptr;
    QPlainTextEdit *editor_ = nullptr;
    QStandardItemModel *model_ = nullptr;

    QJsonDocument rootDoc_;
    std::shared_ptr<LosslessJsonDocument> losslessDoc_;
    QString currentFilePath_;
    QStandardItem *currentItem_ = nullptr;
    bool ignoreEditorChange_ = false;

    QHash<QString, QJsonValue> originalValues_;
    QSet<QStandardItem *> modifiedItems_;
    QHash<QString, QString> reverseMapping_;

    QString lastSearchText_;
    bool lastFindBackward_ = false;
    bool lastFindWrap_ = true;
    bool lastFindCaseSensitive_ = true;
    bool lastFindWholeWord_ = false;
    bool lastFindUseRegex_ = false;
};
