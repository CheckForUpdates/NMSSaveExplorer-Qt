#include "ui/JsonExplorerPage.h"

#include "core/JsonMapper.h"
#include "core/ResourceLocator.h"
#include "core/SaveDecoder.h"
#include "core/SaveEncoder.h"
#include "core/SaveJsonModel.h"
#include "core/Utf8Diagnostics.h"

#include <rapidjson/document.h>

#include <QAbstractItemView>
#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDebug>
#include <QCheckBox>
#include <QFileInfo>
#include <QFile>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QShortcut>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QSplitter>
#include <QTextOption>
#include <QTextCursor>
#include <QTreeView>
#include <QVBoxLayout>

namespace {
constexpr int kPathRole = Qt::UserRole + 1;
constexpr int kModifiedRole = Qt::UserRole + 2;
constexpr int kPopulatedRole = Qt::UserRole + 3;
const char *kMappingFile = "mapping.json";
}

JsonExplorerPage::JsonExplorerPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *split = new QSplitter(this);
    layout->addWidget(split);

    tree_ = new QTreeView(split);
    tree_->setHeaderHidden(true);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);

    editor_ = new QPlainTextEdit(split);
    editor_->setWordWrapMode(QTextOption::NoWrap);

    split->addWidget(tree_);
    split->addWidget(editor_);
    split->setStretchFactor(1, 1);

    model_ = new QStandardItemModel(this);
    tree_->setModel(model_);
    model_->appendRow(new QStandardItem(tr("Open a save file to begin.")));
    editor_->setPlainText(tr("// No save loaded."));

    connect(tree_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &current, const QModelIndex &) {
                QStandardItem *item = model_->itemFromIndex(current);
                if (!item) {
                    return;
                }
                qInfo() << "JsonExplorerPage selection changed to" << item->text();
                if (currentItem_ && currentItem_ != item) {
                    commitEditor();
                }
                currentItem_ = item;
                loadEditorForItem(item);
                emit statusMessage(displayPath(item));
            });

    connect(tree_, &QTreeView::expanded, this, [this](const QModelIndex &index) {
        QStandardItem *item = model_->itemFromIndex(index);
        if (!item) {
            return;
        }
        if (item->data(kPopulatedRole).toBool()) {
            return;
        }
        populateChildren(item);
        item->setData(true, kPopulatedRole);
    });

    connect(editor_, &QPlainTextEdit::textChanged, this, [this]() {
        if (ignoreEditorChange_ || !currentItem_) {
            return;
        }
        QVariantList path = currentItem_->data(kPathRole).toList();
        QJsonValue currentValue = valueAtPath(path);
        QString expected = prettyPrinted(mapToReadable(currentValue));
        if (editor_->toPlainText() == expected) {
            clearModified(currentItem_);
        } else {
            markModified(currentItem_);
        }
    });

    connect(tree_, &QTreeView::customContextMenuRequested, this, [this](const QPoint &pos) {
        QModelIndex index = tree_->indexAt(pos);
        if (!index.isValid()) {
            return;
        }
        QStandardItem *item = model_->itemFromIndex(index);
        if (!item) {
            return;
        }
        QMenu menu(this);
        QAction *revert = menu.addAction(tr("Undo Change"));
        revert->setEnabled(modifiedItems_.contains(item));
        QAction *selected = menu.exec(tree_->viewport()->mapToGlobal(pos));
        if (selected == revert) {
            QVariantList path = item->data(kPathRole).toList();
            QString key = pathKey(path);
            QJsonValue original = originalValues_.value(key);
            if (!original.isUndefined()) {
                if (losslessDoc_) {
                    SaveJsonModel::setLosslessValue(losslessDoc_, path, original);
                    SaveJsonModel::syncRootFromLossless(losslessDoc_, rootDoc_);
                } else {
                    QJsonValue rootValue = rootDoc_.isObject() ? QJsonValue(rootDoc_.object())
                                                              : QJsonValue(rootDoc_.array());
                    QJsonValue updated = setValueAtPath(rootValue, path, 0, original);
                    if (updated.isObject()) {
                        rootDoc_.setObject(updated.toObject());
                    } else if (updated.isArray()) {
                        rootDoc_.setArray(updated.toArray());
                    }
                }
                clearModified(item);
                loadEditorForItem(item);
                emit statusMessage(tr("Reverted node."));
            }
        }
    });

    auto *findShortcut = new QShortcut(QKeySequence::Find, editor_);
    connect(findShortcut, &QShortcut::activated, this, &JsonExplorerPage::showFindDialog);
}


void JsonExplorerPage::setRootDoc(const QJsonDocument &doc, const QString &filePath,
                                  const std::shared_ptr<LosslessJsonDocument> &losslessDoc)
{
    rootDoc_ = doc;
    currentFilePath_ = filePath;
    losslessDoc_ = losslessDoc;
    modifiedItems_.clear();
    originalValues_.clear();
    buildTree();
    emit statusMessage(tr("Loaded %1").arg(QFileInfo(filePath).fileName()));
}

bool JsonExplorerPage::loadFromFile(const QString &filePath, QString *errorMessage)
{
    qInfo() << "JsonExplorerPage::loadFromFile" << filePath;
    ensureMappingLoaded();
    currentFilePath_.clear();

    QByteArray contentBytes;
    if (filePath.endsWith(".hg", Qt::CaseInsensitive)) {
        qInfo() << "Decoding .hg save file.";
        contentBytes = SaveDecoder::decodeSaveBytes(filePath, errorMessage);
    } else {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            if (errorMessage) {
                *errorMessage = tr("Unable to open %1").arg(filePath);
            }
            return false;
        }
        contentBytes = file.readAll();
    }

    qInfo() << "Loaded raw content length:" << contentBytes.size();
    if (contentBytes.isEmpty()) {
        if (errorMessage && errorMessage->isEmpty()) {
            *errorMessage = tr("No data loaded from %1").arg(filePath);
        }
        return false;
    }

    auto lossless = std::make_shared<LosslessJsonDocument>();
    if (!lossless->parse(contentBytes, errorMessage)) {
        return false;
    }

    bool sanitized = false;
    QByteArray qtBytes = sanitizeJsonUtf8ForQt(contentBytes, &sanitized);
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(qtBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (errorMessage) {
            *errorMessage = tr("JSON parse error: %1").arg(parseError.errorString());
        }
        qWarning() << "JSON parse error:" << parseError.errorString();
        logJsonUtf8Error(qtBytes, static_cast<int>(parseError.offset));
        return false;
    }
    if (sanitized) {
        qWarning() << "Sanitized invalid UTF-8 bytes for Qt JSON parser.";
    }

    qInfo() << "JSON parse ok. isObject=" << doc.isObject() << "isArray=" << doc.isArray();
    rootDoc_ = doc;
    losslessDoc_ = lossless;
    currentFilePath_ = filePath;
    if (!syncRootFromLossless(errorMessage)) {
        return false;
    }
    modifiedItems_.clear();
    originalValues_.clear();
    qInfo() << "Building JSON tree.";
    buildTree();
    qInfo() << "JSON tree built.";

    emit statusMessage(tr("Loaded %1").arg(QFileInfo(filePath).fileName()));
    return true;
}

bool JsonExplorerPage::hasLoadedSave() const
{
    return !currentFilePath_.isEmpty() && !rootDoc_.isNull();
}

bool JsonExplorerPage::saveChanges(QString *errorMessage)
{
    if (!hasLoadedSave()) {
        if (errorMessage) {
            *errorMessage = tr("No save loaded.");
        }
        return false;
    }
    commitEditor();

    QString outPath = currentFilePath_;

    if (outPath.endsWith(".json", Qt::CaseInsensitive)) {
        QFile file(outPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            if (errorMessage) {
                *errorMessage = tr("Unable to write %1").arg(outPath);
            }
            return false;
        }
        if (losslessDoc_) {
            file.write(losslessDoc_->toJson(true));
        } else {
            QJsonObject rootObject = rootDoc_.isObject() ? rootDoc_.object() : QJsonObject();
            QJsonDocument doc(remapToShort(rootObject).toObject());
            file.write(doc.toJson(QJsonDocument::Indented));
        }
    } else {
        if (losslessDoc_) {
            if (!SaveEncoder::encodeSave(outPath, losslessDoc_->toJson(false), errorMessage)) {
                return false;
            }
        } else {
            QJsonObject rootObject = rootDoc_.isObject() ? rootDoc_.object() : QJsonObject();
            QJsonObject encoded = rootObject;
            if (!SaveEncoder::encodeSave(outPath, encoded, errorMessage)) {
                return false;
            }
        }
    }

    for (QStandardItem *item : modifiedItems_) {
        clearModified(item);
    }
    modifiedItems_.clear();
    emit statusMessage(tr("Save complete."));
    return true;
}

bool JsonExplorerPage::saveAs(const QString &filePath, QString *errorMessage)
{
    if (!hasLoadedSave()) {
        if (errorMessage) {
            *errorMessage = tr("No save loaded.");
        }
        return false;
    }
    commitEditor();
    if (losslessDoc_) {
        if (!SaveEncoder::encodeSave(filePath, losslessDoc_->toJson(false), errorMessage)) {
            return false;
        }
    } else {
        QJsonObject rootObject = rootDoc_.isObject() ? rootDoc_.object() : QJsonObject();
        QJsonObject encoded = remapToShort(rootObject).toObject();
        if (!SaveEncoder::encodeSave(filePath, encoded, errorMessage)) {
            return false;
        }
    }
    emit statusMessage(tr("Saved %1").arg(QFileInfo(filePath).fileName()));
    return true;
}

bool JsonExplorerPage::exportJson(const QString &filePath, QString *errorMessage) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = tr("Unable to write %1").arg(filePath);
        }
        return false;
    }
    file.write(editor_->toPlainText().toUtf8());
    return true;
}

bool JsonExplorerPage::hasUnsavedChanges() const
{
    return !modifiedItems_.isEmpty();
}

void JsonExplorerPage::expandAll()
{
    tree_->expandAll();
}

void JsonExplorerPage::collapseAll()
{
    tree_->collapseAll();
}

void JsonExplorerPage::buildTree()
{
    qInfo() << "JsonExplorerPage::buildTree start.";
    currentItem_ = nullptr;
    if (tree_->selectionModel()) {
        tree_->selectionModel()->clearSelection();
    }
    model_->clear();
    originalValues_.clear();

    QString rootLabel = QFileInfo(currentFilePath_).fileName();
    QStandardItem *rootItem = new QStandardItem(rootLabel);
    model_->appendRow(rootItem);

    QVariantList rootPath;
    QJsonValue rootValue = rootDoc_.isObject() ? QJsonValue(rootDoc_.object())
                                               : QJsonValue(rootDoc_.array());
    rootItem->setData(rootPath, kPathRole);
    rootItem->setData(false, kPopulatedRole);
    originalValues_.insert(pathKey(rootPath), rootValue);
    addPlaceholderIfNeeded(rootItem, rootValue);

    tree_->expandToDepth(0);
    tree_->setCurrentIndex(rootItem->index());
    qInfo() << "JsonExplorerPage::buildTree done.";
}

void JsonExplorerPage::populateChildren(QStandardItem *parent)
{
    if (!parent) {
        return;
    }
    QVariantList path = parent->data(kPathRole).toList();
    const rapidjson::Value *losslessValue = losslessValueAtPath(path);
    if (losslessValue) {
        parent->removeRows(0, parent->rowCount());

        if (losslessValue->IsObject()) {
            for (auto it = losslessValue->MemberBegin(); it != losslessValue->MemberEnd(); ++it) {
                QString key = QString::fromUtf8(it->name.GetString(),
                                                static_cast<int>(it->name.GetStringLength()));
                QString mapped = JsonMapper::mapKey(key);
                QVariantList childPath = path;
                childPath.append(key);
                QStandardItem *child = new QStandardItem(mapped);
                child->setData(childPath, kPathRole);
                child->setData(false, kPopulatedRole);
                parent->appendRow(child);
                originalValues_.insert(pathKey(childPath), valueAtPath(childPath));
                if (it->value.IsObject() || it->value.IsArray()) {
                    child->appendRow(new QStandardItem(tr("Loading...")));
                }
            }
            return;
        }
        if (losslessValue->IsArray()) {
            for (rapidjson::SizeType i = 0; i < losslessValue->Size(); ++i) {
                QVariantList childPath = path;
                childPath.append(static_cast<int>(i));
                QStandardItem *child = new QStandardItem(QString("[%1]").arg(i));
                child->setData(childPath, kPathRole);
                child->setData(false, kPopulatedRole);
                parent->appendRow(child);
                originalValues_.insert(pathKey(childPath), valueAtPath(childPath));
                const rapidjson::Value &element = (*losslessValue)[i];
                if (element.IsObject() || element.IsArray()) {
                    child->appendRow(new QStandardItem(tr("Loading...")));
                }
            }
            return;
        }
    }

    QJsonValue value = valueAtPath(path);
    parent->removeRows(0, parent->rowCount());

    if (value.isObject()) {
        QJsonObject obj = value.toObject();
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            QString mapped = JsonMapper::mapKey(it.key());
            QVariantList childPath = path;
            childPath.append(it.key());
            QStandardItem *child = new QStandardItem(mapped);
            child->setData(childPath, kPathRole);
            child->setData(false, kPopulatedRole);
            parent->appendRow(child);
            originalValues_.insert(pathKey(childPath), it.value());
            addPlaceholderIfNeeded(child, it.value());
        }
    } else if (value.isArray()) {
        QJsonArray array = value.toArray();
        for (int i = 0; i < array.size(); ++i) {
            QVariantList childPath = path;
            childPath.append(i);
            QStandardItem *child = new QStandardItem(QString("[%1]").arg(i));
            child->setData(childPath, kPathRole);
            child->setData(false, kPopulatedRole);
            parent->appendRow(child);
            QJsonValue element = array.at(i);
            originalValues_.insert(pathKey(childPath), element);
            addPlaceholderIfNeeded(child, element);
        }
    }
}

const rapidjson::Value *JsonExplorerPage::losslessValueAtPath(const QVariantList &path) const
{
    if (!losslessDoc_) {
        return nullptr;
    }
    const rapidjson::Value *node = &losslessDoc_->root();
    for (const QVariant &segment : path) {
        if (segment.canConvert<int>()) {
            if (!node->IsArray()) {
                return nullptr;
            }
            int index = segment.toInt();
            if (index < 0 || index >= static_cast<int>(node->Size())) {
                return nullptr;
            }
            node = &(*node)[static_cast<rapidjson::SizeType>(index)];
            continue;
        }
        if (segment.canConvert<QString>()) {
            if (!node->IsObject()) {
                return nullptr;
            }
            QByteArray key = segment.toString().toUtf8();
            auto it = node->FindMember(key.constData());
            if (it == node->MemberEnd()) {
                return nullptr;
            }
            node = &it->value;
            continue;
        }
        return nullptr;
    }
    return node;
}

void JsonExplorerPage::addPlaceholderIfNeeded(QStandardItem *item, const QJsonValue &value)
{
    if (!item) {
        return;
    }
    if (value.isObject() || value.isArray()) {
        item->appendRow(new QStandardItem(tr("Loading...")));
    }
}

QJsonValue JsonExplorerPage::valueAtPath(const QVariantList &path) const
{
    QJsonValue current = rootDoc_.isObject() ? QJsonValue(rootDoc_.object())
                                             : QJsonValue(rootDoc_.array());
    for (const QVariant &segment : path) {
        if (segment.canConvert<int>() && current.isArray()) {
            QJsonArray array = current.toArray();
            int index = segment.toInt();
            if (index < 0 || index >= array.size()) {
                return QJsonValue();
            }
            current = array.at(index);
        } else if (segment.canConvert<QString>() && current.isObject()) {
            QJsonObject obj = current.toObject();
            QString key = segment.toString();
            current = obj.value(key);
        } else {
            return QJsonValue();
        }
    }
    return current;
}

QJsonValue JsonExplorerPage::remapToShort(const QJsonValue &value) const
{
    if (!value.isObject() && !value.isArray()) {
        return value;
    }

    struct Frame {
        enum class Type { Object, Array };
        Type type;
        QJsonObject srcObj;
        QStringList keys;
        QJsonArray srcArr;
        int idx = 0;
        QJsonObject outObj;
        QJsonArray outArr;
        QString keyForParent;
    };

    QVector<Frame> stack;
    stack.reserve(64);

    auto pushFrame = [&stack](const QJsonValue &src, const QString &keyForParent) {
        Frame f;
        f.keyForParent = keyForParent;
        if (src.isObject()) {
            f.type = Frame::Type::Object;
            f.srcObj = src.toObject();
            f.keys = f.srcObj.keys();
            f.idx = 0;
        } else {
            f.type = Frame::Type::Array;
            f.srcArr = src.toArray();
            f.idx = 0;
        }
        stack.push_back(std::move(f));
    };

    pushFrame(value, QString());

    while (!stack.isEmpty()) {
        int topIdx = stack.size() - 1;
        
        if (stack[topIdx].type == Frame::Type::Object) {
            if (stack[topIdx].idx >= stack[topIdx].keys.size()) {
                QJsonValue result = stack[topIdx].outObj;
                QString key = stack[topIdx].keyForParent;
                stack.removeLast();
                if (stack.isEmpty()) return result;
                
                if (stack.last().type == Frame::Type::Object) {
                    stack.last().outObj.insert(key, result);
                } else {
                    stack.last().outArr.append(result);
                }
                continue;
            }

            QString srcKey = stack[topIdx].keys.at(stack[topIdx].idx);
            QJsonValue srcVal = stack[topIdx].srcObj.value(srcKey);
            QString shortKey = reverseMapping_.value(srcKey, srcKey);
            stack[topIdx].idx++;

            if (srcVal.isObject() || srcVal.isArray()) {
                pushFrame(srcVal, shortKey);
            } else {
                stack[topIdx].outObj.insert(shortKey, srcVal);
            }
        } else {
            if (stack[topIdx].idx >= stack[topIdx].srcArr.size()) {
                QJsonValue result = stack[topIdx].outArr;
                QString key = stack[topIdx].keyForParent;
                stack.removeLast();
                if (stack.isEmpty()) return result;

                if (stack.last().type == Frame::Type::Object) {
                    stack.last().outObj.insert(key, result);
                } else {
                    stack.last().outArr.append(result);
                }
                continue;
            }

            QJsonValue srcVal = stack[topIdx].srcArr.at(stack[topIdx].idx);
            stack[topIdx].idx++;

            if (srcVal.isObject() || srcVal.isArray()) {
                pushFrame(srcVal, QString());
            } else {
                stack[topIdx].outArr.append(srcVal);
            }
        }
    }

    return value;
}

QJsonValue JsonExplorerPage::mapToReadable(const QJsonValue &value) const
{
    if (!value.isObject() && !value.isArray()) {
        return value;
    }

    struct Frame {
        enum class Type { Object, Array };
        Type type;
        QJsonObject srcObj;
        QStringList keys;
        QJsonArray srcArr;
        int idx = 0;
        QJsonObject outObj;
        QJsonArray outArr;
        QString keyForParent;
    };

    QVector<Frame> stack;
    stack.reserve(64);

    auto pushFrame = [&stack](const QJsonValue &src, const QString &keyForParent) {
        Frame f;
        f.keyForParent = keyForParent;
        if (src.isObject()) {
            f.type = Frame::Type::Object;
            f.srcObj = src.toObject();
            f.keys = f.srcObj.keys();
            f.idx = 0;
        } else {
            f.type = Frame::Type::Array;
            f.srcArr = src.toArray();
            f.idx = 0;
        }
        stack.push_back(std::move(f));
    };

    pushFrame(value, QString());

    while (!stack.isEmpty()) {
        int topIdx = stack.size() - 1;
        
        if (stack[topIdx].type == Frame::Type::Object) {
            if (stack[topIdx].idx >= stack[topIdx].keys.size()) {
                QJsonValue result = stack[topIdx].outObj;
                QString key = stack[topIdx].keyForParent;
                stack.removeLast();
                if (stack.isEmpty()) return result;
                
                if (stack.last().type == Frame::Type::Object) {
                    stack.last().outObj.insert(key, result);
                } else {
                    stack.last().outArr.append(result);
                }
                continue;
            }

            QString srcKey = stack[topIdx].keys.at(stack[topIdx].idx);
            QJsonValue srcVal = stack[topIdx].srcObj.value(srcKey);
            QString mappedKey = JsonMapper::mapKey(srcKey);
            stack[topIdx].idx++;

            if (srcVal.isObject() || srcVal.isArray()) {
                pushFrame(srcVal, mappedKey);
            } else {
                stack[topIdx].outObj.insert(mappedKey, srcVal);
            }
        } else {
            if (stack[topIdx].idx >= stack[topIdx].srcArr.size()) {
                QJsonValue result = stack[topIdx].outArr;
                QString key = stack[topIdx].keyForParent;
                stack.removeLast();
                if (stack.isEmpty()) return result;

                if (stack.last().type == Frame::Type::Object) {
                    stack.last().outObj.insert(key, result);
                } else {
                    stack.last().outArr.append(result);
                }
                continue;
            }

            QJsonValue srcVal = stack[topIdx].srcArr.at(stack[topIdx].idx);
            stack[topIdx].idx++;

            if (srcVal.isObject() || srcVal.isArray()) {
                pushFrame(srcVal, QString());
            } else {
                stack[topIdx].outArr.append(srcVal);
            }
        }
    }

    return value;
}

QJsonValue JsonExplorerPage::setValueAtPath(const QJsonValue &root, const QVariantList &path,
                                            int depth, const QJsonValue &value) const
{
    if (depth >= path.size()) {
        return value;
    }

    QVariant segment = path.at(depth);
    if (segment.canConvert<int>() && root.isArray()) {
        QJsonArray array = root.toArray();
        int index = segment.toInt();
        if (index >= 0 && index < array.size()) {
            array[index] = setValueAtPath(array.at(index), path, depth + 1, value);
        }
        return array;
    }
    if (segment.canConvert<QString>() && root.isObject()) {
        QJsonObject obj = root.toObject();
        QString key = segment.toString();
        obj.insert(key, setValueAtPath(obj.value(key), path, depth + 1, value));
        return obj;
    }
    return root;
}

QString JsonExplorerPage::pathKey(const QVariantList &path) const
{
    QStringList parts;
    for (const QVariant &segment : path) {
        if (segment.canConvert<int>()) {
            parts << QString("[%1]").arg(segment.toInt());
        } else {
            parts << segment.toString();
        }
    }
    return parts.join("/");
}

QString JsonExplorerPage::displayPath(QStandardItem *item) const
{
    if (!item) {
        return QString();
    }
    QStringList parts;
    QStandardItem *current = item;
    while (current) {
        QString label = current->text();
        if (label.endsWith('*')) {
            label.chop(1);
        }
        parts.prepend(label);
        current = current->parent();
    }
    return parts.join("/");
}

void JsonExplorerPage::loadEditorForItem(QStandardItem *item)
{
    if (!item) {
        return;
    }
    QVariantList path = item->data(kPathRole).toList();
    QJsonValue value = valueAtPath(path);
    
    ignoreEditorChange_ = true;
    editor_->setReadOnly(false);
    QString text = prettyPrinted(mapToReadable(value));
    if (text.isEmpty() && !value.isNull() && !value.isUndefined()) {
        qWarning() << "JsonExplorerPage::loadEditorForItem: prettyPrinted returned empty for non-null value at" << path;
    }
    editor_->setPlainText(text);
    ignoreEditorChange_ = false;
}

bool JsonExplorerPage::commitEditor()
{
    if (!currentItem_ || !modifiedItems_.contains(currentItem_)) {
        return false;
    }

    if (editor_->isReadOnly()) {
        emit statusMessage(tr("Read-only preview. Select a child node to edit."));
        return false;
    }

    if (model_->itemFromIndex(currentItem_->index()) != currentItem_) {
        qWarning() << "JsonExplorerPage::commitEditor current item no longer valid.";
        return false;
    }

    QString text = editor_->toPlainText().trimmed();
    if (text.isEmpty()) {
        return false;
    }

    QJsonParseError error;
    QJsonDocument parsed = QJsonDocument::fromJson(text.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError) {
        emit statusMessage(tr("Invalid JSON: %1").arg(error.errorString()));
        return false;
    }

    QJsonValue newValue;
    if (parsed.isObject()) {
        newValue = parsed.object();
    } else if (parsed.isArray()) {
        QJsonArray arr = parsed.array();
        if (arr.size() == 1 && !arr.at(0).isArray() && !arr.at(0).isObject()) {
            newValue = arr.at(0);
        } else {
            newValue = arr;
        }
    }

    QJsonValue remapped = remapToShort(newValue);
    QVariantList path = currentItem_->data(kPathRole).toList();
    QJsonValue currentValue = valueAtPath(path);

    if (currentValue == remapped) {
        clearModified(currentItem_);
        return true;
    }

    if (losslessDoc_) {
        SaveJsonModel::setLosslessValue(losslessDoc_, path, remapped);
        SaveJsonModel::syncRootFromLossless(losslessDoc_, rootDoc_);
    } else {
        QJsonValue rootValue = rootDoc_.isObject() ? QJsonValue(rootDoc_.object())
                                                   : QJsonValue(rootDoc_.array());
        QJsonValue updated = setValueAtPath(rootValue, path, 0, remapped);
        if (updated.isObject()) {
            rootDoc_.setObject(updated.toObject());
        } else if (updated.isArray()) {
            rootDoc_.setArray(updated.toArray());
        }
    }

    markModified(currentItem_);
    return true;
}

void JsonExplorerPage::markModified(QStandardItem *item)
{
    if (!item) {
        return;
    }
    if (!modifiedItems_.contains(item)) {
        modifiedItems_.insert(item);
        QString text = item->text();
        if (!text.endsWith('*')) {
            item->setText(text + '*');
        }
    }
}

void JsonExplorerPage::clearModified(QStandardItem *item)
{
    if (!item) {
        return;
    }
    QString text = item->text();
    if (text.endsWith('*')) {
        item->setText(text.left(text.size() - 1));
    }
    modifiedItems_.remove(item);
}

QString JsonExplorerPage::prettyPrinted(const QJsonValue &value) const
{
    if (value.isNull() || value.isUndefined()) {
        return QString();
    }
    
    QJsonDocument doc;
    if (value.isObject()) {
        doc.setObject(value.toObject());
    } else if (value.isArray()) {
        doc.setArray(value.toArray());
    } else {
        QJsonArray arr;
        arr.append(value);
        doc.setArray(arr);
    }
    
    QByteArray bytes = doc.toJson(QJsonDocument::Indented);
    
    // Safety check for extremely large strings that might crash the editor or memory allocation
    if (bytes.size() > 50 * 1024 * 1024) {
        qWarning() << "JSON too large for editor, truncating.";
        return QString::fromUtf8(bytes.left(50 * 1024 * 1024)) + "\n\n// ... truncated due to size ...";
    }
    
    return QString::fromUtf8(bytes);
}

void JsonExplorerPage::showFindDialog()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Find in JSON"));

    auto *layout = new QVBoxLayout(&dialog);
    auto *field = new QLineEdit(&dialog);
    field->setPlaceholderText(tr("Enter text to find..."));
    field->setText(lastSearchText_);
    layout->addWidget(field);

    auto *optionsLayout = new QGridLayout();
    auto *caseSensitive = new QCheckBox(tr("Case sensitive"), &dialog);
    caseSensitive->setChecked(lastFindCaseSensitive_);
    auto *wholeWord = new QCheckBox(tr("Whole word"), &dialog);
    wholeWord->setChecked(lastFindWholeWord_);
    auto *wrap = new QCheckBox(tr("Wrap search"), &dialog);
    wrap->setChecked(lastFindWrap_);
    auto *useRegex = new QCheckBox(tr("Use regular expression"), &dialog);
    useRegex->setChecked(lastFindUseRegex_);

    optionsLayout->addWidget(caseSensitive, 0, 0);
    optionsLayout->addWidget(wholeWord, 0, 1);
    optionsLayout->addWidget(wrap, 1, 0);
    optionsLayout->addWidget(useRegex, 1, 1);
    layout->addLayout(optionsLayout);

    auto *directionGroup = new QGroupBox(tr("Direction"), &dialog);
    auto *directionLayout = new QHBoxLayout(directionGroup);
    auto *forward = new QRadioButton(tr("Forward"), directionGroup);
    auto *backward = new QRadioButton(tr("Backward"), directionGroup);
    forward->setChecked(!lastFindBackward_);
    backward->setChecked(lastFindBackward_);
    directionLayout->addWidget(forward);
    directionLayout->addWidget(backward);
    directionLayout->addStretch();
    layout->addWidget(directionGroup);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Find Next"));
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, &dialog, [this, field, backward, wrap, caseSensitive, wholeWord, useRegex]() {
        performFind(field->text(),
                    backward->isChecked(),
                    wrap->isChecked(),
                    caseSensitive->isChecked(),
                    wholeWord->isChecked(),
                    useRegex->isChecked());
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    dialog.exec();
}

void JsonExplorerPage::performFind(const QString &text, bool backward, bool wrap, bool caseSensitive, bool wholeWord, bool useRegex)
{
    if (text.isEmpty()) {
        return;
    }

    bool optionsChanged = text != lastSearchText_ ||
                          backward != lastFindBackward_ ||
                          caseSensitive != lastFindCaseSensitive_ ||
                          wholeWord != lastFindWholeWord_ ||
                          useRegex != lastFindUseRegex_;

    lastSearchText_ = text;
    lastFindBackward_ = backward;
    lastFindWrap_ = wrap;
    lastFindCaseSensitive_ = caseSensitive;
    lastFindWholeWord_ = wholeWord;
    lastFindUseRegex_ = useRegex;

    QTextDocument::FindFlags flags;
    if (backward) {
        flags |= QTextDocument::FindBackward;
    }
    if (caseSensitive) {
        flags |= QTextDocument::FindCaseSensitively;
    }
    if (!useRegex && wholeWord) {
        flags |= QTextDocument::FindWholeWords;
    }

    QTextCursor startCursor = editor_->textCursor();
    if (optionsChanged) {
        if (backward) {
            startCursor.movePosition(QTextCursor::End);
        } else {
            startCursor.movePosition(QTextCursor::Start);
        }
    }

    QRegularExpression regex;
    if (useRegex) {
        QString pattern = text;
        if (wholeWord) {
            pattern = QStringLiteral("\\b(?:%1)\\b").arg(pattern);
        }
        regex = QRegularExpression(pattern);
        if (!caseSensitive) {
            regex.setPatternOptions(regex.patternOptions() | QRegularExpression::CaseInsensitiveOption);
        }
        if (!regex.isValid()) {
            emit statusMessage(tr("Invalid regular expression: %1").arg(regex.errorString()));
            return;
        }
    }

    auto findFrom = [&](const QTextCursor &cursor) -> QTextCursor {
        if (useRegex) {
            return editor_->document()->find(regex, cursor, flags);
        }
        return editor_->document()->find(text, cursor, flags);
    };

    QTextCursor found = findFrom(startCursor);
    if (found.isNull() && wrap) {
        QTextCursor wrapCursor = editor_->textCursor();
        if (backward) {
            wrapCursor.movePosition(QTextCursor::End);
        } else {
            wrapCursor.movePosition(QTextCursor::Start);
        }
        found = findFrom(wrapCursor);
    }

    if (!found.isNull()) {
        editor_->setTextCursor(found);
        emit statusMessage(tr("Found \"%1\"").arg(text));
    } else {
        emit statusMessage(tr("No matches for \"%1\"").arg(text));
    }
}

void JsonExplorerPage::ensureMappingLoaded()
{
    if (JsonMapper::isLoaded()) {
        qInfo() << "JsonMapper already loaded.";
        return;
    }
    QString mappingPath = ResourceLocator::resolveResource(kMappingFile);
    qInfo() << "Loading mapping from" << mappingPath;
    JsonMapper::loadMapping(mappingPath);
    reverseMapping_.clear();
    QHash<QString, QString> map = JsonMapper::mapping();
    qInfo() << "Mapping size:" << map.size();
    for (auto it = map.begin(); it != map.end(); ++it) {
        reverseMapping_.insert(it.value(), it.key());
    }
}

bool JsonExplorerPage::syncRootFromLossless(QString *errorMessage)
{
    return SaveJsonModel::syncRootFromLossless(losslessDoc_, rootDoc_, errorMessage);
}
