#include "inventory/KnownProductPage.h"

#include "core/SaveDecoder.h"
#include "core/SaveEncoder.h"
#include "core/SaveJsonModel.h"
#include "core/Utf8Diagnostics.h"
#include "inventory/InventoryEditorPage.h"
#include "inventory/KnownProductDialog.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QVBoxLayout>

namespace
{
const char *kKeyActiveContext = "XTp";
const char *kKeyExpeditionContext = "2YS";
const char *kKeyPlayerState = "vLc";
const char *kContextMain = "Main";
const char *kKeyKnownProducts = "eZ<";
} // namespace

KnownProductPage::KnownProductPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
}

bool KnownProductPage::loadFromFile(const QString &filePath, QString *errorMessage)
{
    QByteArray contentBytes;
    if (filePath.endsWith(".hg", Qt::CaseInsensitive)) {
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
        logJsonUtf8Error(qtBytes, static_cast<int>(parseError.offset));
        return false;
    }
    if (sanitized) {
        qWarning() << "Sanitized invalid UTF-8 bytes for Qt JSON parser.";
    }

    rootDoc_ = doc;
    losslessDoc_ = lossless;
    currentFilePath_ = filePath;
    hasUnsavedChanges_ = false;
    if (!syncRootFromLossless(errorMessage)) {
        return false;
    }
    updateActiveContext();

    QVariantList knownPath = playerBasePath();
    knownPath << kKeyKnownProducts;
    QJsonValue knownValue = InventoryEditorPage::valueAtPath(rootDoc_.object(), knownPath);
    QJsonArray known = knownValue.isArray() ? knownValue.toArray() : QJsonArray();
    resetEditor(known, knownPath);

    emit statusMessage(tr("Loaded %1").arg(QFileInfo(filePath).fileName()));
    return true;
}

bool KnownProductPage::hasLoadedSave() const
{
    return !currentFilePath_.isEmpty() && !rootDoc_.isNull();
}

bool KnownProductPage::hasUnsavedChanges() const
{
    return hasUnsavedChanges_;
}

const QString &KnownProductPage::currentFilePath() const
{
    return currentFilePath_;
}

bool KnownProductPage::saveChanges(QString *errorMessage)
{
    if (!hasLoadedSave()) {
        if (errorMessage) {
            *errorMessage = tr("No save loaded.");
        }
        return false;
    }

    if (currentFilePath_.endsWith(".json", Qt::CaseInsensitive)) {
        QFile file(currentFilePath_);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            if (errorMessage) {
                *errorMessage = tr("Unable to write %1").arg(currentFilePath_);
            }
            return false;
        }
        if (losslessDoc_) {
            file.write(losslessDoc_->toJson(true));
        } else {
            QJsonObject rootObject = rootDoc_.isObject() ? rootDoc_.object() : QJsonObject();
            QJsonDocument doc(rootObject);
            file.write(doc.toJson(QJsonDocument::Indented));
        }
        hasUnsavedChanges_ = false;
        return true;
    }

    if (losslessDoc_) {
        if (!SaveEncoder::encodeSave(currentFilePath_, losslessDoc_->toJson(false), errorMessage)) {
            return false;
        }
        hasUnsavedChanges_ = false;
        return true;
    }

    QJsonObject rootObject = rootDoc_.isObject() ? rootDoc_.object() : QJsonObject();
    if (!SaveEncoder::encodeSave(currentFilePath_, rootObject, errorMessage)) {
        return false;
    }
    hasUnsavedChanges_ = false;
    return true;
}

void KnownProductPage::updateActiveContext()
{
    usingExpeditionContext_ = false;
    if (!rootDoc_.isObject()) {
        return;
    }
    QJsonObject root = rootDoc_.object();
    if (!root.contains(kKeyExpeditionContext)) {
        return;
    }
    QJsonValue contextValue = root.value(kKeyActiveContext);
    QString context = contextValue.toString();
    QString normalized = context.trimmed().toLower();
    if (normalized.isEmpty() || normalized == QString(kContextMain).toLower()) {
        return;
    }
    QJsonObject expedition = root.value(kKeyExpeditionContext).toObject();
    if (expedition.contains("6f=")) {
        usingExpeditionContext_ = true;
    }
}

QVariantList KnownProductPage::playerBasePath() const
{
    if (usingExpeditionContext_) {
        return {kKeyExpeditionContext, "6f="};
    }
    return {kKeyPlayerState, "6f="};
}

void KnownProductPage::applyValueAtPath(const QVariantList &path, const QJsonValue &value)
{
    QJsonValue rootValue = rootDoc_.isObject() ? QJsonValue(rootDoc_.object())
                                               : QJsonValue(rootDoc_.array());
    if (InventoryEditorPage::valueAtPath(rootValue, path) == value) {
        return;
    }

    if (!losslessDoc_) {
        QJsonValue updated = InventoryEditorPage::setValueAtPath(rootValue, path, 0, value);
        if (updated.isObject()) {
            rootDoc_.setObject(updated.toObject());
        } else if (updated.isArray()) {
            rootDoc_.setArray(updated.toArray());
        }
        hasUnsavedChanges_ = true;
        return;
    }

    QVariantList remappedCheck = SaveJsonModel::remapPathToShort(path);
    if (remappedCheck != path && InventoryEditorPage::valueAtPath(rootValue, remappedCheck) == value) {
        return;
    }

    bool wrote = SaveJsonModel::setLosslessValue(losslessDoc_, path, value);
    QVariantList remapped;
    if (!wrote) {
        remapped = SaveJsonModel::remapPathToShort(path);
    }

    if (!wrote) {
        QJsonValue updated = InventoryEditorPage::setValueAtPath(rootValue, path, 0, value);
        if (updated.isObject()) {
            rootDoc_.setObject(updated.toObject());
        } else if (updated.isArray()) {
            rootDoc_.setArray(updated.toArray());
        }
        if (remapped.isEmpty()) {
            remapped = SaveJsonModel::remapPathToShort(path);
        }
        QString topKey;
        if (!path.isEmpty() && path.first().canConvert<QString>()) {
            topKey = path.first().toString();
        }
        QString remappedTop;
        if (!remapped.isEmpty() && remapped.first().canConvert<QString>()) {
            remappedTop = remapped.first().toString();
        }
        QJsonObject rootObj = rootDoc_.object();
        QJsonValue topValue = (!topKey.isEmpty()) ? rootObj.value(topKey) : QJsonValue();
        if (topValue.isUndefined() && !remappedTop.isEmpty() && remappedTop != topKey) {
            topValue = rootObj.value(remappedTop);
        }
        if (!topValue.isUndefined()) {
            if (!remappedTop.isEmpty()) {
                losslessDoc_->setValueAtPath({remappedTop}, topValue);
            } else if (!topKey.isEmpty()) {
                losslessDoc_->setValueAtPath({topKey}, topValue);
            }
        }
    }

    SaveJsonModel::syncRootFromLossless(losslessDoc_, rootDoc_);
    hasUnsavedChanges_ = true;
}

bool KnownProductPage::syncRootFromLossless(QString *errorMessage)
{
    return SaveJsonModel::syncRootFromLossless(losslessDoc_, rootDoc_, errorMessage);
}

void KnownProductPage::resetEditor(const QJsonArray &knownProducts, const QVariantList &knownPath)
{
    if (editor_) {
        layout()->removeWidget(editor_);
        editor_->deleteLater();
        editor_ = nullptr;
    }

    editor_ = new KnownProductDialog(knownProducts, this);
    layout()->addWidget(editor_);

    connect(editor_, &KnownProductDialog::knownProductsChanged, this, [this, knownPath](const QJsonArray &updated) {
        applyValueAtPath(knownPath, updated);
        emit statusMessage(tr("Known products updated. Pending changes — remember to Save!"));
    });
}
