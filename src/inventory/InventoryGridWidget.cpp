#include "inventory/InventoryGridWidget.h"

#include "inventory/ItemSelectionDialog.h"
#include "registry/IconRegistry.h"
#include "registry/ItemCatalog.h"
#include "registry/ItemDefinitionRegistry.h"

#include <QDrag>
#include <QEvent>
#include <QGridLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QSizePolicy>
#include <QPainter>
#include <QStyleOption>
#include <QVBoxLayout>

namespace {
constexpr int kIconSize = 72;
const char *kMimeType = "application/x-nms-slot";

QString normalizedItemId(const QJsonObject &item);
bool isDamageSlotPlaceholderId(const QString &id);

ItemType itemTypeFromValue(const QString &value)
{
    QString lower = value.trimmed().toLower();
    if (lower == "substance") {
        return ItemType::Substance;
    }
    if (lower == "product") {
        return ItemType::Product;
    }
    if (lower == "technology") {
        return ItemType::Technology;
    }
    return ItemType::Unknown;
}

QString inventoryValueForType(ItemType type)
{
    switch (type) {
    case ItemType::Substance:
        return QStringLiteral("Substance");
    case ItemType::Product:
        return QStringLiteral("Product");
    case ItemType::Technology:
        return QStringLiteral("Technology");
    default:
        return QStringLiteral("Unknown");
    }
}

bool allowSuperchargeForTitle(const QString &title)
{
    QString lower = title.trimmed().toLower();
    if (lower.contains("multitool") || lower.contains("multi-tool")) {
        return true;
    }
    if (lower.contains("technology")) {
        return true;
    }
    return false;
}

bool isDamagedItem(const QJsonObject &item, const QString &inventoryTitle)
{
    QString id = normalizedItemId(item);
    if (isDamageSlotPlaceholderId(id)) {
        return true;
    }

    QJsonObject typeObj = item.value("Vn8").toObject();
    ItemType type = itemTypeFromValue(typeObj.value("elv").toString());
    QString lowerTitle = inventoryTitle.trimmed().toLower();
    bool isTechContext = (type == ItemType::Technology)
        || lowerTitle.contains("technology")
        || lowerTitle.contains("tech");

    if (!isTechContext) {
        return false;
    }

    if (item.contains("eVk") && item.value("eVk").toDouble(0.0) > 0.0) {
        return true;
    }
    if (item.contains("b76") && !item.value("b76").toBool(true)) {
        return true;
    }
    return false;
}

QString normalizedItemId(const QJsonObject &item)
{
    QString id = item.value("b2n").toString();
    if (id.startsWith('^')) {
        id.remove(0, 1);
    }
    return id;
}

bool isDamageSlotPlaceholderId(const QString &id)
{
    static const QStringList kDamagePrefixes = {
        QStringLiteral("SHIPSLOT_DMG"),
        QStringLiteral("SHIPEASY_DMG"),
        QStringLiteral("WEAPSLOT_DMG"),
        QStringLiteral("WEAPEASY_DMG"),
        QStringLiteral("WEAPSENT_DMG")
    };

    for (const QString &prefix : kDamagePrefixes) {
        if (id.startsWith(prefix)) {
            return true;
        }
    }
    static const QRegularExpression kDamageSuffix(QStringLiteral(".*_DMG\\d+$"),
                                                  QRegularExpression::CaseInsensitiveOption);
    if (kDamageSuffix.match(id).hasMatch()) {
        return true;
    }
    return false;
}

QString specialSlotTypeValue(const QJsonObject &special)
{
    QJsonValue value = special.value("QA1");
    if (value.isUndefined()) {
        value = special.value("InventorySpecialSlotType");
    }
    if (value.isString()) {
        return value.toString().trimmed();
    }
    if (value.isDouble()) {
        return QString::number(static_cast<int>(value.toDouble()));
    }
    return QString();
}

bool isSuperchargedSlot(const QJsonObject &special)
{
    QJsonValue raw = special.value("QA1");
    if (raw.isUndefined()) {
        raw = special.value("InventorySpecialSlotType");
    }
    if (raw.isDouble()) {
        return static_cast<int>(raw.toDouble()) != 0;
    }
    QString type = specialSlotTypeValue(special);
    if (type.isEmpty()) {
        return true;
    }
    return type.compare("Supercharged", Qt::CaseInsensitive) == 0
        || type.compare("SuperchargedSlot", Qt::CaseInsensitive) == 0
        || type.compare("SuperchargedSlotType", Qt::CaseInsensitive) == 0;
}

QJsonObject specialSlotIndexValue(const QJsonObject &special)
{
    QJsonValue value = special.value("3ZH");
    if (value.isUndefined()) {
        value = special.value("Index");
    }
    return value.toObject();
}

int indexValue(const QJsonObject &idx, const char *shortKey, const char *longKey)
{
    if (idx.contains(shortKey)) {
        return qRound(idx.value(shortKey).toDouble(-1));
    }
    return qRound(idx.value(longKey).toDouble(-1));
}

bool specialSlotsUseLongKeys(const QJsonArray &specialSlots)
{
    for (const QJsonValue &value : specialSlots) {
        if (!value.isObject()) {
            continue;
        }
        QJsonObject special = value.toObject();
        if (special.contains("InventorySpecialSlotType") || special.contains("Index")) {
            return true;
        }
    }
    return false;
}

QJsonObject validSlotIndexValue(const QJsonValue &value)
{
    if (!value.isObject()) {
        return {};
    }
    QJsonObject obj = value.toObject();
    if (obj.contains("Index")) {
        return obj.value("Index").toObject();
    }
    if (obj.contains("3ZH")) {
        return obj.value("3ZH").toObject();
    }
    return obj;
}

bool validSlotsUseIndexObject(const QJsonArray &validSlots)
{
    for (const QJsonValue &value : validSlots) {
        if (!value.isObject()) {
            continue;
        }
        if (value.toObject().contains("Index")) {
            return true;
        }
    }
    return false;
}

bool validSlotsUseLongKeys(const QJsonArray &validSlots)
{
    for (const QJsonValue &value : validSlots) {
        QJsonObject idx = validSlotIndexValue(value);
        if (idx.contains("X") || idx.contains("Y")) {
            return true;
        }
    }
    return false;
}
}

InventoryGridWidget::InventoryGridWidget(QWidget *parent)
    : QWidget(parent)
{
    grid_ = new QGridLayout(this);
    grid_->setContentsMargins(kGridMargin, kGridMargin, kGridMargin, kGridMargin);
    grid_->setSpacing(kGridSpacing);

    nameOverlay_ = new QLabel(this);
    nameOverlay_->setStyleSheet("background-color: rgba(0, 0, 0, 180); color: white; padding: 4px 10px; border-radius: 4px; border: none; font-weight: bold;");
    nameOverlay_->setAttribute(Qt::WA_TransparentForMouseEvents);
    nameOverlay_->setAlignment(Qt::AlignCenter);
    nameOverlay_->hide();
}

void InventoryGridWidget::setInventory(const QString &title, const QJsonArray &slotsArray,
                                       const QJsonArray &validSlots, const QJsonArray &specialSlots)
{
    title_ = title;
    slots_ = slotsArray;
    validSlots_ = validSlots;
    specialSlots_ = specialSlots;
    rebuild();
}

void InventoryGridWidget::setCommitHandler(const std::function<void(const QJsonArray &, const QJsonArray &, const QJsonArray &)> &handler)
{
    commitHandler_ = handler;
}

void InventoryGridWidget::setShowIds(bool show)
{
    if (showIds_ == show) {
        return;
    }
    showIds_ = show;
    hideNameOverlay();
}

void InventoryGridWidget::rebuild()
{
    while (QLayoutItem *item = grid_->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }

    int maxX = kGridWidth - 1;
    int maxY = 0;

    auto updateMax = [&](const QJsonObject &idx) {
        if (idx.isEmpty()) return;
        maxX = qMax(maxX, indexValue(idx, ">Qh", "X"));
        maxY = qMax(maxY, indexValue(idx, "XJ>", "Y"));
    };

    for (const QJsonValue &value : validSlots_) {
        if (value.isObject()) updateMax(value.toObject());
    }
    for (const QJsonValue &value : slots_) {
        if (!value.isObject()) {
            continue;
        }
        QJsonObject item = value.toObject();
        QJsonValue idxValue = item.value("3ZH");
        if (idxValue.isUndefined()) {
            idxValue = item.value("Index");
        }
        updateMax(idxValue.toObject());
    }
    for (const QJsonValue &value : specialSlots_) {
        if (!value.isObject()) {
            continue;
        }
        updateMax(specialSlotIndexValue(value.toObject()));
    }

    int gridWidth = maxX + 1;
    int gridHeight = maxY + 1;
    if (validSlots_.isEmpty()) {
        gridHeight = qMax(gridHeight, 6);
    }

    for (int y = 0; y < gridHeight; ++y) {
        for (int x = 0; x < gridWidth; ++x) {
            auto *cell = new InventoryCell(x, y, this);
            cell->setFixedSize(kCellSize, kCellSize);
            bool slotEnabled = isSlotEnabled(x, y);
            cell->setSlotEnabled(slotEnabled);
            cell->setDropEnabled(slotEnabled);
            cell->setDragEnabled(slotEnabled);
            attachContextMenu(cell);
            grid_->addWidget(cell, y, x);
        }
    }

    for (const QJsonValue &value : slots_) {
        if (!value.isObject()) continue;
        QJsonObject item = value.toObject();
        QJsonValue idxValue = item.value("3ZH");
        if (idxValue.isUndefined()) {
            idxValue = item.value("Index");
        }
        QJsonObject idx = idxValue.toObject();
        int x = indexValue(idx, ">Qh", "X");
        int y = indexValue(idx, "XJ>", "Y");

        if (x < 0 || y < 0) continue;

        QLayoutItem *layoutItem = grid_->itemAtPosition(y, x);
        auto *cell = layoutItem ? static_cast<InventoryCell *>(layoutItem->widget()) : nullptr;
        if (cell) {
            cell->setContent(item, kIconSize);
        }
    }

    for (const QJsonValue &value : specialSlots_) {
        if (!value.isObject()) continue;
        QJsonObject special = value.toObject();

        if (!isSuperchargedSlot(special)) {
            continue;
        }

        QJsonObject idx = specialSlotIndexValue(special);
        int x = indexValue(idx, ">Qh", "X");
        int y = indexValue(idx, "XJ>", "Y");

        if (x < 0 || y < 0) continue;

        QLayoutItem *layoutItem = grid_->itemAtPosition(y, x);
        auto *cell = layoutItem ? static_cast<InventoryCell *>(layoutItem->widget()) : nullptr;
        if (cell) {
            cell->setSupercharged(true);
        }
    }

    const int width = kGridMargin * 2 + gridWidth * kCellSize + (gridWidth - 1) * kGridSpacing;
    const int height = kGridMargin * 2 + gridHeight * kCellSize + (gridHeight - 1) * kGridSpacing;
    setMinimumSize(width, height);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    emit statusMessage(tr("Inventory ready."));
}

int InventoryGridWidget::preferredGridWidth()
{
    return kGridWidth * kCellSize + (kGridWidth - 1) * kGridSpacing + (kGridMargin * 2);
}

int InventoryGridWidget::preferredGridHeight(int rows)
{
    if (rows < 1) {
        rows = 1;
    }
    return rows * kCellSize + (rows - 1) * kGridSpacing + (kGridMargin * 2);
}

void InventoryGridWidget::attachContextMenu(InventoryCell *cell)
{
    cell->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(cell, &QWidget::customContextMenuRequested, this, [this, cell](const QPoint &pos) {
        QMenu menu;
        bool slotEnabled = cell->isSlotEnabled();
        QAction *infoAction = menu.addAction(tr("Info..."));
        QAction *changeAmount = menu.addAction(tr("Change Amount..."));
        QAction *maxAmount = menu.addAction(tr("Max Amount"));
        QAction *deleteAction = menu.addAction(tr("Delete Item"));
        QAction *addAction = menu.addAction(tr("Add Item..."));
        QAction *enableSlotAction = nullptr;
        if (!slotEnabled) {
            enableSlotAction = menu.addAction(tr("Enable Slot"));
        }
        menu.addSeparator();
        QAction *repairAction = nullptr;
        QAction *repairAllAction = nullptr;
        QAction *superchargeAction = nullptr;
        if (cell->isDamaged()) {
            repairAction = menu.addAction(tr("Repair"));
        }
        for (const QJsonValue &value : slots_) {
            if (value.isObject() && isDamagedItem(value.toObject(), title_)) {
                repairAllAction = menu.addAction(tr("Repair All Damaged"));
                break;
            }
        }
        if (allowSuperchargeForTitle(title_)) {
            superchargeAction = menu.addAction(cell->isSupercharged() ? tr("Remove Supercharge") : tr("Supercharge"));
        }

        bool supportsAmount = cell->supportsAmount();
        infoAction->setEnabled(cell->hasItem());
        changeAmount->setVisible(supportsAmount);
        maxAmount->setVisible(supportsAmount);
        
        changeAmount->setEnabled(cell->hasItem());
        maxAmount->setEnabled(cell->hasItem());
        deleteAction->setEnabled(cell->hasItem());
        addAction->setEnabled(slotEnabled && !cell->hasItem());
        
        if (repairAction) {
            repairAction->setEnabled(cell->hasItem());
        }
        if (repairAllAction) {
            repairAllAction->setEnabled(true);
        }
        if (superchargeAction) {
            superchargeAction->setEnabled(slotEnabled);
        }

        QAction *selected = menu.exec(cell->mapToGlobal(pos));
        if (selected == infoAction) {
            showItemInfo(cell);
        } else if (selected == changeAmount) {
            changeItemAmount(cell);
        } else if (selected == maxAmount) {
            maxItemAmount(cell);
        } else if (selected == deleteAction) {
            deleteItem(cell);
        } else if (selected == addAction) {
            addItem(cell);
        } else if (enableSlotAction && selected == enableSlotAction) {
            enableSlot(cell);
        } else if (repairAction && selected == repairAction) {
            repairItem(cell);
        } else if (repairAllAction && selected == repairAllAction) {
            repairAllDamaged();
        } else if (superchargeAction && selected == superchargeAction) {
            toggleSupercharged(cell);
        }
    });

    cell->setAcceptDrops(true);
}

void InventoryGridWidget::showItemInfo(InventoryCell *cell)
{
    if (!cell || !cell->hasItem()) {
        return;
    }
    QJsonObject item = cell->currentItem();
    QString rawId = item.value("b2n").toString();
    QString id = normalizedItemId(item);
    QString idLabel = id.isEmpty() ? rawId : id;

    QJsonObject typeObj = item.value("Vn8").toObject();
    QString typeRaw = typeObj.value("elv").toString();
    ItemType type = itemTypeFromValue(typeRaw);
    QString typeLabel = inventoryValueForType(type);
    if (type == ItemType::Unknown && !typeRaw.isEmpty()) {
        typeLabel = typeRaw;
    }

    QString displayName;
    if (showIds_) {
        displayName = idLabel;
    } else {
        displayName = ItemDefinitionRegistry::displayNameForId(rawId);
        if (displayName.isEmpty()) {
            displayName = ItemDefinitionRegistry::displayNameForId(id);
        }
        if (displayName.isEmpty()) {
            displayName = cell->displayName();
        }
        if (displayName.isEmpty()) {
            displayName = tr("Unknown");
        }
    }

    QString text = tr("Name: %1\nID: %2\nType: %3").arg(displayName, idLabel, typeLabel);
    QMessageBox::information(this, tr("Item Info"), text);
}

void InventoryGridWidget::moveOrSwap(int srcX, int srcY, int dstX, int dstY)
{
    int srcIndex = -1;
    int dstIndex = -1;
    QJsonObject srcItem;
    QJsonObject dstItem;

    for (int i = 0; i < slots_.size(); ++i) {
        QJsonObject obj = slots_.at(i).toObject();
        QJsonObject idx = obj.value("3ZH").toObject();
        if (!idx.contains(">Qh") || !idx.contains("XJ>")) {
            continue;
        }
        int x = idx.value(">Qh").toInt();
        int y = idx.value("XJ>").toInt();
        if (x == srcX && y == srcY) {
            srcIndex = i;
            srcItem = obj;
        } else if (x == dstX && y == dstY) {
            dstIndex = i;
            dstItem = obj;
        }
    }

    if (srcIndex < 0) {
        return;
    }

    QJsonObject srcIdx = srcItem.value("3ZH").toObject();
    srcIdx.insert(">Qh", dstX);
    srcIdx.insert("XJ>", dstY);
    srcItem.insert("3ZH", srcIdx);

    if (dstIndex >= 0) {
        QJsonObject dstIdx = dstItem.value("3ZH").toObject();
        dstIdx.insert(">Qh", srcX);
        dstIdx.insert("XJ>", srcY);
        dstItem.insert("3ZH", dstIdx);

        slots_.replace(srcIndex, dstItem);
        slots_.replace(dstIndex, srcItem);
    } else {
        slots_.replace(srcIndex, srcItem);
    }

    if (commitHandler_) {
        commitHandler_(slots_, validSlots_, specialSlots_);
    }
    rebuild();
    emit statusMessage(tr("Pending changes — remember to Save!"));
}

void InventoryGridWidget::changeItemAmount(InventoryCell *cell)
{
    if (!cell || !cell->hasItem()) {
        return;
    }
    QJsonObject item = cell->currentItem();
    int currentAmount = item.value("1o9").toInt(1);
    bool ok = false;
    int updated = QInputDialog::getInt(this, tr("Change Amount"), tr("Amount:"),
                                       currentAmount, 1, 999999, 1, &ok);
    if (!ok) {
        return;
    }

    int index = -1;
    QJsonObject found = findItemAt(cell->position().x, cell->position().y, &index);
    if (index < 0) {
        return;
    }
    found.insert("1o9", updated);
    int currentMax = found.value("F9q").toInt(0);
    if (updated > currentMax) {
        found.insert("F9q", updated);
    }
    slots_.replace(index, found);
    if (commitHandler_) {
        commitHandler_(slots_, validSlots_, specialSlots_);
    }
    rebuild();
    emit statusMessage(tr("Pending changes — remember to Save!"));
}

void InventoryGridWidget::maxItemAmount(InventoryCell *cell)
{
    if (!cell || !cell->hasItem()) {
        return;
    }
    int index = -1;
    QJsonObject found = findItemAt(cell->position().x, cell->position().y, &index);
    if (index < 0) {
        return;
    }
    int maxAmount = found.value("F9q").toInt(0);
    if (maxAmount > 0) {
        found.insert("1o9", maxAmount);
        slots_.replace(index, found);
        if (commitHandler_) {
            commitHandler_(slots_, validSlots_, specialSlots_);
        }
        rebuild();
        emit statusMessage(tr("Item maxed — remember to Save!"));
    } else {
        emit statusMessage(tr("Item has no defined max amount."));
    }
}

void InventoryGridWidget::deleteItem(InventoryCell *cell)
{
    if (!cell) {
        return;
    }
    int index = -1;
    findItemAt(cell->position().x, cell->position().y, &index);
    if (index < 0) {
        return;
    }
    slots_.removeAt(index);
    if (commitHandler_) {
        commitHandler_(slots_, validSlots_, specialSlots_);
    }
    rebuild();
    emit statusMessage(tr("Pending changes — remember to Save!"));
}

void InventoryGridWidget::addItem(InventoryCell *cell)
{
    if (!cell) {
        return;
    }
    if (!cell->isSlotEnabled()) {
        emit statusMessage(tr("Slot is disabled."));
        return;
    }

    QList<ItemType> allowedTypes;
    QString lower = title_.toLower();
    
    if (lower.contains("technology") || lower.contains("tech") || lower.contains("multi") || lower.contains("weapon")) {
        allowedTypes.append(ItemType::Technology);
    }
    
    if (!lower.contains("technology") && !lower.contains("tech-only") && !lower.contains("multi")) {
        allowedTypes.append(ItemType::Substance);
        allowedTypes.append(ItemType::Product);
    }

    if (allowedTypes.isEmpty()) {
        for (const QJsonValue &value : slots_) {
            if (!value.isObject()) {
                continue;
            }
            QJsonObject obj = value.toObject();
            QJsonObject typeObj = obj.value("Vn8").toObject();
            ItemType type = itemTypeFromValue(typeObj.value("elv").toString());
            if (type != ItemType::Unknown && !allowedTypes.contains(type)) {
                allowedTypes.append(type);
            }
        }
    }

    if (allowedTypes.isEmpty()) {
        allowedTypes.append(ItemType::Substance);
        allowedTypes.append(ItemType::Product);
        allowedTypes.append(ItemType::Technology);
    }

    QList<ItemEntry> entries = ItemCatalog::itemsForTypes(allowedTypes);
    if (entries.isEmpty()) {
        emit statusMessage(tr("No catalog entries available."));
        return;
    }

    ItemSelectionDialog dialog(entries, this);
    if (dialog.exec() != QDialog::Accepted || !dialog.hasSelection()) {
        return;
    }

    ItemSelectionResult selection = dialog.selection();
    QString id = selection.entry.id;
    if (!id.startsWith('^')) {
        id.prepend('^');
    }

    QJsonObject newItem;
    newItem.insert("b2n", id);

    QJsonObject type;
    type.insert("elv", inventoryValueForType(selection.entry.type));
    newItem.insert("Vn8", type);

    int maxAmount = selection.entry.maxStack > 0 ? selection.entry.maxStack : selection.amount;
    newItem.insert("1o9", maxAmount);
    newItem.insert("F9q", maxAmount);
    newItem.insert("eVk", 0.0);
    newItem.insert("b76", true);

    QJsonObject idx;
    idx.insert(">Qh", cell->position().x);
    idx.insert("XJ>", cell->position().y);
    newItem.insert("3ZH", idx);

    slots_.append(newItem);
    if (commitHandler_) {
        commitHandler_(slots_, validSlots_, specialSlots_);
    }
    rebuild();
    emit statusMessage(tr("Pending changes — remember to Save!"));
}

void InventoryGridWidget::enableSlot(InventoryCell *cell)
{
    if (!cell || validSlots_.isEmpty()) {
        return;
    }
    int x = cell->position().x;
    int y = cell->position().y;
    if (isSlotEnabled(x, y)) {
        return;
    }

    bool useIndexObject = validSlotsUseIndexObject(validSlots_);
    bool useLongKeys = validSlotsUseLongKeys(validSlots_);
    QJsonObject idx;
    if (useLongKeys) {
        idx.insert("X", x);
        idx.insert("Y", y);
    } else {
        idx.insert(">Qh", x);
        idx.insert("XJ>", y);
    }

    QJsonObject entry = useIndexObject ? QJsonObject{{"Index", idx}} : idx;
    validSlots_.append(entry);
    if (commitHandler_) {
        commitHandler_(slots_, validSlots_, specialSlots_);
    }
    rebuild();
    emit statusMessage(tr("Slot enabled — remember to Save!"));
}

void InventoryGridWidget::toggleSupercharged(InventoryCell *cell)
{
    if (!cell) return;
    int x = cell->position().x;
    int y = cell->position().y;

    int foundIndex = -1;
    const bool useLongKeys = specialSlotsUseLongKeys(specialSlots_);
    for (int i = 0; i < specialSlots_.size(); ++i) {
        QJsonObject obj = specialSlots_.at(i).toObject();
        QJsonObject idx = specialSlotIndexValue(obj);
        if (indexValue(idx, ">Qh", "X") == x && indexValue(idx, "XJ>", "Y") == y) {
            foundIndex = i;
            break;
        }
    }

    if (foundIndex >= 0) {
        specialSlots_.removeAt(foundIndex);
        emit statusMessage(tr("Slot supercharge removed — remember to Save!"));
    } else {
        QJsonObject newSpecial;
        QJsonObject idx;
        if (useLongKeys) {
            idx.insert("X", x);
            idx.insert("Y", y);
            newSpecial.insert("Index", idx);
            newSpecial.insert("InventorySpecialSlotType", "Supercharged");
        } else {
            idx.insert(">Qh", x);
            idx.insert("XJ>", y);
            newSpecial.insert("3ZH", idx);
            newSpecial.insert("QA1", "Supercharged");
        }
        specialSlots_.append(newSpecial);
        emit statusMessage(tr("Slot supercharged — remember to Save!"));
    }

    if (commitHandler_) {
        commitHandler_(slots_, validSlots_, specialSlots_);
    }
    rebuild();
}

void InventoryGridWidget::repairItem(InventoryCell *cell)
{
    if (!cell || !cell->hasItem()) {
        return;
    }
    int index = -1;
    QJsonObject found = findItemAt(cell->position().x, cell->position().y, &index);
    if (index < 0) {
        return;
    }
    QString id = normalizedItemId(found);
    if (isDamageSlotPlaceholderId(id)) {
        slots_.removeAt(index);
    } else {
        found.insert("eVk", 0.0);
        found.insert("b76", true);
        int maxAmount = found.value("F9q").toInt(0);
        if (maxAmount > 0) {
            found.insert("1o9", maxAmount);
        }
        slots_.replace(index, found);
    }
    if (commitHandler_) {
        commitHandler_(slots_, validSlots_, specialSlots_);
    }
    rebuild();
    emit statusMessage(tr("Item repaired — remember to Save!"));
}

void InventoryGridWidget::repairAllDamaged()
{
    if (slots_.isEmpty()) {
        return;
    }

    QJsonArray updatedSlots;
    int removedCount = 0;
    int repairedCount = 0;

    for (const QJsonValue &value : slots_) {
        if (!value.isObject()) {
            updatedSlots.append(value);
            continue;
        }
        QJsonObject item = value.toObject();
        if (!isDamagedItem(item, title_)) {
            updatedSlots.append(item);
            continue;
        }

        QString id = normalizedItemId(item);
        if (isDamageSlotPlaceholderId(id)) {
            ++removedCount;
            continue;
        }

        item.insert("eVk", 0.0);
        item.insert("b76", true);
        int maxAmount = item.value("F9q").toInt(0);
        if (maxAmount > 0) {
            item.insert("1o9", maxAmount);
        }
        ++repairedCount;
        updatedSlots.append(item);
    }

    if (removedCount == 0 && repairedCount == 0) {
        emit statusMessage(tr("No damaged items found."));
        return;
    }

    slots_ = updatedSlots;
    if (commitHandler_) {
        commitHandler_(slots_, validSlots_, specialSlots_);
    }
    rebuild();
    if (removedCount > 0 && repairedCount > 0) {
        emit statusMessage(tr("Repaired %1 item(s), cleared %2 slot(s) — remember to Save!")
                               .arg(repairedCount)
                               .arg(removedCount));
    } else if (repairedCount > 0) {
        emit statusMessage(tr("Repaired %1 item(s) — remember to Save!").arg(repairedCount));
    } else {
        emit statusMessage(tr("Cleared %1 damaged slot(s) — remember to Save!").arg(removedCount));
    }
}

void InventoryGridWidget::showNameOverlay(InventoryCell *cell)
{
    if (!cell || !nameOverlay_) return;
    if (!cell->hasItem()) return;
    QString text = showIds_ ? cell->itemId() : cell->displayName();
    if (text.isEmpty()) {
        text = showIds_ ? cell->displayName() : cell->itemId();
    }
    if (text.isEmpty()) return;

    nameOverlay_->setText(text);
    nameOverlay_->adjustSize();

    QPoint cellPos = cell->pos();
    int x = cellPos.x() + (cell->width() - nameOverlay_->width()) / 2;
    int y = cellPos.y() + 5;

    nameOverlay_->move(x, y);
    nameOverlay_->show();
    nameOverlay_->raise();
}

void InventoryGridWidget::hideNameOverlay()
{
    if (nameOverlay_) {
        nameOverlay_->hide();
    }
}

bool InventoryGridWidget::isSlotEnabled(int x, int y) const
{
    if (validSlots_.isEmpty()) {
        return true;
    }
    for (const QJsonValue &value : validSlots_) {
        QJsonObject idx = validSlotIndexValue(value);
        int slotX = indexValue(idx, ">Qh", "X");
        int slotY = indexValue(idx, "XJ>", "Y");
        if (slotX == x && slotY == y) {
            return true;
        }
    }
    return false;
}

QJsonObject InventoryGridWidget::findItemAt(int x, int y, int *index) const
{
    for (int i = 0; i < slots_.size(); ++i) {
        QJsonObject obj = slots_.at(i).toObject();
        QJsonValue idxValue = obj.value("3ZH");
        if (idxValue.isUndefined()) {
            idxValue = obj.value("Index");
        }
        QJsonObject idx = idxValue.toObject();
        if (indexValue(idx, ">Qh", "X") == x && indexValue(idx, "XJ>", "Y") == y) {
            if (index) {
                *index = i;
            }
            return obj;
        }
    }
    if (index) {
        *index = -1;
    }
    return {};
}

InventoryGridWidget::InventoryCell::InventoryCell(int x, int y, QWidget *parent)
    : QWidget(parent), x_(x), y_(y)
{
    setAcceptDrops(true);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    iconLabel_ = new QLabel(this);
    iconLabel_->setAlignment(Qt::AlignCenter);
    amountLabel_ = new QLabel(this);
    amountLabel_->setAlignment(Qt::AlignCenter);

    layout->addWidget(iconLabel_, 0, Qt::AlignCenter);
    layout->addWidget(amountLabel_, 0, Qt::AlignCenter);

    setStyleSheet("background-color: #1f1f1f; border: 1px solid #2b2b2b;");
    iconLabel_->setStyleSheet("border: none; background: transparent;");
    amountLabel_->setStyleSheet("border: none; background: transparent; color: #e6e6e6;");
}

void InventoryGridWidget::InventoryCell::setSupercharged(bool supercharged)
{
    supercharged_ = supercharged;
    update();
}

void InventoryGridWidget::InventoryCell::setSlotEnabled(bool enabled)
{
    slotEnabled_ = enabled;
    if (slotEnabled_) {
        setStyleSheet("background-color: #1f1f1f; border: 1px solid #2b2b2b;");
    } else {
        setStyleSheet("background-color: #5a5a5a; border: 1px solid #6b6b6b;");
    }
}

void InventoryGridWidget::InventoryCell::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    if (slotEnabled_) {
        QStyleOption option;
        option.initFrom(this);
        style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
    } else {
        painter.fillRect(rect(), QColor(90, 90, 90));
        painter.setPen(QColor(107, 107, 107));
        painter.drawRect(rect().adjusted(0, 0, -1, -1));
    }
    if (damaged_) {
        painter.setRenderHint(QPainter::Antialiasing);
        QColor glowColor(220, 45, 45, 160);
        QPen glowPen(glowColor, 2);
        painter.setPen(glowPen);
        painter.drawRect(rect().adjusted(1, 1, -1, -1));
        painter.fillRect(rect().adjusted(1, 1, -1, -1), QColor(220, 45, 45, 24));
    } else if (supercharged_) {
        painter.setRenderHint(QPainter::Antialiasing);
        
        QColor glowColor(0, 170, 255, 140); 
        QPen glowPen(glowColor, 2);
        painter.setPen(glowPen);
        painter.drawRect(rect().adjusted(1, 1, -1, -1));

        painter.fillRect(rect().adjusted(1, 1, -1, -1), QColor(0, 170, 255, 20));
    }
}

void InventoryGridWidget::InventoryCell::setContent(const QJsonObject &item, int iconSize)
{
    item_ = item;
    auto *grid = qobject_cast<InventoryGridWidget *>(parentWidget());
    damaged_ = isDamagedItem(item_, grid ? grid->title() : QString());
    QString rawId = item.value("b2n").toString();
    QString normalized = normalizedItemId(item);
    id_ = normalized.isEmpty() ? rawId : normalized;
    int amount = item.value("1o9").toInt(1);
    int max = item.value("F9q").toInt(0);

    QPixmap icon = IconRegistry::iconForId(rawId);
    if (icon.isNull() && rawId.startsWith('^')) {
        icon = IconRegistry::iconForId(rawId.mid(1));
    }
    if (!icon.isNull()) {
        iconLabel_->setPixmap(icon.scaled(iconSize, iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        iconLabel_->setPixmap(QPixmap());
    }

    amountLabel_->setText(max > 0 ? QString("%1/%2").arg(amount).arg(max) : QString::number(amount));
    if (amount == -1) {
        amountLabel_->hide();
    } else {
        amountLabel_->show();
    }

    QString displayName = ItemDefinitionRegistry::displayNameForId(rawId);
    if (displayName.isEmpty()) {
        displayName = ItemDefinitionRegistry::displayNameForId(id_);
    }
    displayName_ = displayName.isEmpty() ? (id_.isEmpty() ? rawId : id_) : displayName;
}

bool InventoryGridWidget::InventoryCell::supportsAmount() const
{
    if (!hasItem()) return false;
    return item_.value("1o9").toInt(1) != -1;
}

void InventoryGridWidget::InventoryCell::setEmpty()
{
    item_ = {};
    damaged_ = false;
    iconLabel_->setPixmap(QPixmap());
    amountLabel_->setText(QString());
    amountLabel_->show();
    displayName_ = QString();
    id_ = QString();
}

void InventoryGridWidget::InventoryCell::mousePressEvent(QMouseEvent *event)
{
    if (!dragEnabled_ || !hasItem() || event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    auto *drag = new QDrag(this);
    auto *mime = new QMimeData();
    mime->setData(kMimeType, QByteArray::number(x_) + "," + QByteArray::number(y_));
    drag->setMimeData(mime);
    drag->exec(Qt::MoveAction);
}

void InventoryGridWidget::InventoryCell::enterEvent(
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QEnterEvent *event
#else
    QEvent *event
#endif
)
{
    Q_UNUSED(event);
    if (auto *grid = qobject_cast<InventoryGridWidget *>(parentWidget())) {
        grid->showNameOverlay(this);
    }
}

void InventoryGridWidget::InventoryCell::leaveEvent(QEvent *event)
{
    Q_UNUSED(event);
    if (auto *grid = qobject_cast<InventoryGridWidget *>(parentWidget())) {
        grid->hideNameOverlay();
    }
}

void InventoryGridWidget::InventoryCell::dragEnterEvent(QDragEnterEvent *event)
{
    if (dropEnabled_ && event->mimeData()->hasFormat(kMimeType)) {
        event->acceptProposedAction();
        return;
    }
    QWidget::dragEnterEvent(event);
}

void InventoryGridWidget::InventoryCell::dragMoveEvent(QDragMoveEvent *event)
{
    if (dropEnabled_ && event->mimeData()->hasFormat(kMimeType)) {
        event->acceptProposedAction();
        return;
    }
    QWidget::dragMoveEvent(event);
}

void InventoryGridWidget::InventoryCell::dropEvent(QDropEvent *event)
{
    if (!event->mimeData()->hasFormat(kMimeType)) {
        QWidget::dropEvent(event);
        return;
    }

    QByteArray payload = event->mimeData()->data(kMimeType);
    QList<QByteArray> parts = payload.split(',');
    if (parts.size() != 2) {
        return;
    }
    bool okX = false;
    bool okY = false;
    int srcX = parts.at(0).toInt(&okX);
    int srcY = parts.at(1).toInt(&okY);
    if (!okX || !okY) {
        return;
    }

    auto *grid = qobject_cast<InventoryGridWidget *>(parentWidget());
    if (!grid) {
        grid = qobject_cast<InventoryGridWidget *>(parentWidget()->parentWidget());
    }
    if (grid) {
        grid->moveOrSwap(srcX, srcY, x_, y_);
    }
    event->acceptProposedAction();
}
