#include "inventory/KnownProductDialog.h"

#include "core/ResourceLocator.h"
#include "registry/IconRegistry.h"
#include "registry/ItemCatalog.h"
#include "registry/ItemDefinitionRegistry.h"

#include <algorithm>
#include <QDialog>
#include <QDomDocument>
#include <QFile>
#include <QHeaderView>
#include <QLineEdit>
#include <QPushButton>
#include <QMessageBox>
#include <QSet>
#include <QTableWidget>
#include <QHBoxLayout>
#include <QTimer>
#include <QVBoxLayout>

namespace
{
QString normalizeIdForLookup(const QString &value)
{
    QString id = value.trimmed();
    if (id.startsWith('^')) {
        id = id.mid(1);
    }
    int hashIndex = id.indexOf('#');
    if (hashIndex >= 0) {
        id = id.left(hashIndex);
    }
    return id.toUpper();
}

QString humanizeCategory(const QString &value)
{
    if (value.isEmpty()) {
        return QString();
    }
    QString out;
    out.reserve(value.size() + 4);
    QChar prev;
    for (int i = 0; i < value.size(); ++i) {
        QChar ch = value.at(i);
        if (ch == '_' || ch == '-') {
            out.append(' ');
            prev = ch;
            continue;
        }
        if (i > 0 && ch.isUpper() && prev.isLower()) {
            out.append(' ');
        } else if (i > 0 && ch.isDigit() && !prev.isDigit()) {
            out.append(' ');
        }
        out.append(ch);
        prev = ch;
    }
    return out.trimmed();
}

void loadCategoriesFromTable(QHash<QString, QString> &categories, const QString &filename, const QString &entryValue, const QString &categoryPropName)
{
    const QString path = ResourceLocator::resolveResource(QString("data/%1").arg(filename));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    QDomDocument doc;
    if (!doc.setContent(&file)) {
        return;
    }

    QDomNodeList nodes = doc.elementsByTagName("Property");
    for (int i = 0; i < nodes.count(); ++i) {
        QDomElement element = nodes.at(i).toElement();
        if (element.isNull() || element.attribute("value") != entryValue) {
            continue;
        }

        QString id = element.attribute("_id");
        if (id.isEmpty()) {
            QDomElement child = element.firstChildElement("Property");
            while (!child.isNull()) {
                if (child.attribute("name") == "ID") {
                    id = child.attribute("value");
                    break;
                }
                child = child.nextSiblingElement("Property");
            }
        }
        id = normalizeIdForLookup(id);
        if (id.isEmpty()) {
            continue;
        }

        QString category;
        QDomElement child = element.firstChildElement("Property");
        while (!child.isNull()) {
            if (child.attribute("name") == categoryPropName) {
                category = child.attribute("value");
                break;
            }
            child = child.nextSiblingElement("Property");
        }

        category = humanizeCategory(category);
        if (!category.isEmpty()) {
            categories.insert(id, category);
        }
    }
}

QHash<QString, QString> loadProductCategories()
{
    QHash<QString, QString> categories;
    loadCategoriesFromTable(categories, "nms_reality_gcproducttable.MXML", "GcProductData", "Category");
    loadCategoriesFromTable(categories, "nms_reality_gcsubstancetable.MXML", "GcRealitySubstanceData", "Category");
    return categories;
}

const QHash<QString, QString> &productCategories()
{
    static QHash<QString, QString> cache = loadProductCategories();
    return cache;
}

} // namespace

KnownProductDialog::KnownProductDialog(const QJsonArray &knownProducts, QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(820, 540);

    for (const QJsonValue &value : knownProducts) {
        if (!value.isString()) {
            continue;
        }
        knownIds_.append(value.toString());
    }

    auto *layout = new QVBoxLayout(this);
    auto *searchRow = new QHBoxLayout();
    searchField_ = new QLineEdit(this);
    searchField_->setPlaceholderText(tr("Search by name or ID..."));
    searchField_->setFixedWidth(360);
    table_ = new QTableWidget(this);
    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels({tr("Known"), tr("Name"), tr("Category"), tr("ID")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSortingEnabled(true);
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);

    addButton_ = new QPushButton(tr("Add Product"), this);
    removeButton_ = new QPushButton(tr("Remove Selected"), this);
    addButton_->setFixedSize(140, 26);
    removeButton_->setFixedSize(140, 26);
    searchRow->addWidget(searchField_);
    searchRow->addWidget(addButton_);
    searchRow->addWidget(removeButton_);
    searchRow->addStretch();
    layout->addLayout(searchRow);
    layout->addWidget(table_);

    connect(addButton_, &QPushButton::clicked, this, &KnownProductDialog::addProduct);
    connect(removeButton_, &QPushButton::clicked, this, &KnownProductDialog::removeSelected);
    connect(table_->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this]() {
        refreshRemoveEnabled();
    });
    connect(searchField_, &QLineEdit::textChanged, this, &KnownProductDialog::filterTable);

    allEntries_ = ItemCatalog::itemsForTypes({ItemType::Product, ItemType::Substance});
    for (ItemEntry &entry : allEntries_) {
        if (entry.displayName.isEmpty()) {
            entry.displayName = ItemDefinitionRegistry::displayNameForId(entry.id);
        }
    }
    std::sort(allEntries_.begin(), allEntries_.end(), [](const ItemEntry &a, const ItemEntry &b) {
        const QString an = a.displayName.isEmpty() ? a.id : a.displayName;
        const QString bn = b.displayName.isEmpty() ? b.id : b.displayName;
        return an.toLower() < bn.toLower();
    });

    rebuildTable();
    refreshRemoveEnabled();
}

QJsonArray KnownProductDialog::updatedProducts() const
{
    QJsonArray result;
    for (const QString &id : knownIds_) {
        result.append(id);
    }
    return result;
}

bool KnownProductDialog::hasChanges() const
{
    return hasChanges_;
}

void KnownProductDialog::rebuildTable()
{
    const QHash<QString, QString> &categories = productCategories();
    QSet<QString> knownSet;
    for (const QString &id : knownIds_) {
        knownSet.insert(normalizedId(id));
    }

    table_->setSortingEnabled(false);
    table_->setRowCount(allEntries_.size());
    for (int row = 0; row < allEntries_.size(); ++row) {
        const ItemEntry &entry = allEntries_.at(row);
        const QString normalized = normalizedId(entry.id);
        QString name = entry.displayName.isEmpty() ? normalized : entry.displayName;
        QString category = categories.value(normalized);
        if (category.isEmpty()) {
            category = tr("Unknown");
        }

        const bool isKnown = knownSet.contains(normalized);
        auto *knownItem = new QTableWidgetItem(isKnown ? tr("Yes") : tr("No"));
        knownItem->setTextAlignment(Qt::AlignCenter);
        const QColor knownColor = isKnown ? QColor(46, 165, 81) : QColor(196, 64, 64);
        knownItem->setForeground(knownColor);
        auto *nameItem = new QTableWidgetItem(name);
        QPixmap icon = IconRegistry::iconForId(entry.id);
        if (!icon.isNull()) {
            nameItem->setIcon(QIcon(icon));
        }
        auto *categoryItem = new QTableWidgetItem(category);
        auto *idItem = new QTableWidgetItem(normalized);
        idItem->setData(Qt::UserRole, entry.id);

        table_->setItem(row, 0, knownItem);
        table_->setItem(row, 1, nameItem);
        table_->setItem(row, 2, categoryItem);
        table_->setItem(row, 3, idItem);
    }
    table_->setSortingEnabled(true);
    table_->sortItems(1, Qt::AscendingOrder);
}

void KnownProductDialog::refreshRemoveEnabled()
{
    if (!removeButton_) {
        return;
    }
    removeButton_->setEnabled(!table_->selectionModel()->selectedRows().isEmpty());
}

void KnownProductDialog::addProduct()
{
    QModelIndexList selectedRows = table_->selectionModel()->selectedRows();
    QStringList addIds;
    QStringList addNames;
    QSet<QString> knownSet;
    for (const QString &id : knownIds_) {
        knownSet.insert(normalizedId(id));
    }

    for (const QModelIndex &index : selectedRows) {
        int row = index.row();
        if (table_->isRowHidden(row)) {
            continue;
        }
        QTableWidgetItem *idItem = table_->item(row, 3);
        QTableWidgetItem *nameItem = table_->item(row, 1);
        if (!idItem) {
            continue;
        }
        QString selected = idItem->data(Qt::UserRole).toString();
        if (selected.isEmpty()) {
            selected = idItem->text();
        }
        const QString normalized = normalizedId(selected);
        if (normalized.isEmpty()) {
            continue;
        }
        if (knownSet.contains(normalized)) {
            continue;
        }
        if (!selected.startsWith('^')) {
            selected = QString("^%1").arg(normalized);
        }
        addIds.append(selected);
        addNames.append(nameItem ? nameItem->text() : normalized);
    }

    if (addIds.isEmpty()) {
        QMessageBox::information(this, tr("Nothing to Add"),
                                 tr("Select products that are not known yet."));
        return;
    }

    QString confirmMessage;
    if (addNames.size() == 1) {
        confirmMessage = tr("Add %1 to known products?").arg(addNames.first());
    } else {
        QStringList preview = addNames.mid(0, 6);
        QString details = preview.join("\n");
        if (addNames.size() > preview.size()) {
            details += tr("\n...and %1 more").arg(addNames.size() - preview.size());
        }
        confirmMessage = tr("Add %1 products?\n%2").arg(addNames.size()).arg(details);
    }
    if (QMessageBox::question(this, tr("Confirm Add"), confirmMessage,
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes) {
        return;
    }

    for (const QString &selected : addIds) {
        knownIds_.append(selected);
    }
    rebuildTable();
    hasChanges_ = true;
    emit knownProductsChanged(updatedProducts());
}

void KnownProductDialog::removeSelected()
{
    QList<int> rows;
    for (const QModelIndex &index : table_->selectionModel()->selectedRows()) {
        int row = index.row();
        if (table_->isRowHidden(row)) {
            continue;
        }
        rows.append(row);
    }
    if (rows.isEmpty()) {
        return;
    }

    QSet<QString> removeIds;
    QStringList removeNames;
    for (int row : rows) {
        QTableWidgetItem *knownItem = table_->item(row, 0);
        if (!knownItem || knownItem->text() != tr("Yes")) {
            continue;
        }
        QTableWidgetItem *idItem = table_->item(row, 3);
        QTableWidgetItem *nameItem = table_->item(row, 1);
        if (!idItem) {
            continue;
        }
        QString rawId = idItem->data(Qt::UserRole).toString();
        if (rawId.isEmpty()) {
            rawId = idItem->text();
        }
        removeIds.insert(normalizedId(rawId));
        if (nameItem) {
            removeNames.append(nameItem->text());
        } else {
            removeNames.append(rawId);
        }
    }

    if (removeIds.isEmpty()) {
        QMessageBox::information(this, tr("Nothing to Remove"),
                                 tr("Select known products to remove."));
        return;
    }

    QString confirmMessage;
    if (removeNames.size() == 1) {
        confirmMessage = tr("Remove %1 from known products?").arg(removeNames.first());
    } else {
        QStringList preview = removeNames.mid(0, 6);
        QString details = preview.join("\n");
        if (removeNames.size() > preview.size()) {
            details += tr("\n...and %1 more").arg(removeNames.size() - preview.size());
        }
        confirmMessage = tr("Remove %1 products?\n%2").arg(removeNames.size()).arg(details);
    }
    if (QMessageBox::question(this, tr("Confirm Removal"), confirmMessage,
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes) {
        return;
    }

    QStringList filtered;
    filtered.reserve(knownIds_.size());
    for (const QString &value : knownIds_) {
        if (!removeIds.contains(normalizedId(value))) {
            filtered.append(value);
        }
    }
    knownIds_ = filtered;
    rebuildTable();
    refreshRemoveEnabled();
    hasChanges_ = true;
    emit knownProductsChanged(updatedProducts());
}

QString KnownProductDialog::normalizedId(const QString &value) const
{
    return normalizeIdForLookup(value);
}

void KnownProductDialog::filterTable(const QString &text)
{
    const QString needle = text.trimmed().toLower();
    for (int row = 0; row < table_->rowCount(); ++row) {
        QTableWidgetItem *nameItem = table_->item(row, 1);
        QTableWidgetItem *idItem = table_->item(row, 3);
        QString textValue;
        if (nameItem) {
            textValue += nameItem->text();
        }
        if (idItem) {
            textValue += " " + idItem->text();
        }
        bool match = needle.isEmpty() || textValue.toLower().contains(needle);
        table_->setRowHidden(row, !match);
    }
}
