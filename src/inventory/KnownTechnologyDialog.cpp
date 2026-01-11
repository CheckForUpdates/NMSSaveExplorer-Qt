#include "inventory/KnownTechnologyDialog.h"

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

QHash<QString, QString> loadTechnologyCategories()
{
    QHash<QString, QString> categories;
    const QString path = ResourceLocator::resolveResource(QStringLiteral("data/nms_reality_gctechnologytable.MXML"));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return categories;
    }

    QDomDocument doc;
    if (!doc.setContent(&file)) {
        return categories;
    }

    QDomNodeList nodes = doc.elementsByTagName("Property");
    for (int i = 0; i < nodes.count(); ++i) {
        QDomElement element = nodes.at(i).toElement();
        if (element.isNull()) {
            continue;
        }
        if (element.attribute("value") != "GcTechnology") {
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
            if (child.attribute("name") == "Category") {
                QDomElement cat = child.firstChildElement("Property");
                while (!cat.isNull()) {
                    if (cat.attribute("name") == "TechnologyCategory") {
                        category = cat.attribute("value");
                        break;
                    }
                    cat = cat.nextSiblingElement("Property");
                }
                break;
            }
            child = child.nextSiblingElement("Property");
        }

        category = humanizeCategory(category);
        if (!category.isEmpty()) {
            categories.insert(id, category);
        }
    }

    return categories;
}

const QHash<QString, QString> &technologyCategories()
{
    static QHash<QString, QString> cache = loadTechnologyCategories();
    return cache;
}

class TechnologySelectionDialog : public QDialog
{
public:
    explicit TechnologySelectionDialog(const QList<ItemEntry> &entries, QWidget *parent = nullptr)
        : QDialog(parent), entries_(entries)
    {
        setWindowTitle(tr("Add Technology"));
        setMinimumSize(820, 560);

        auto *layout = new QVBoxLayout(this);
        searchField_ = new QLineEdit(this);
        searchField_->setPlaceholderText(tr("Search by name or ID..."));

        table_ = new QTableWidget(this);
        table_->setColumnCount(3);
        table_->setHorizontalHeaderLabels({tr("Name"), tr("Category"), tr("ID")});
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        table_->setSelectionMode(QAbstractItemView::SingleSelection);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table_->setSortingEnabled(true);
        table_->horizontalHeader()->setStretchLastSection(false);
        table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

        auto *controls = new QHBoxLayout();
        auto *addButton = new QPushButton(tr("Add"), this);
        auto *cancelButton = new QPushButton(tr("Cancel"), this);
        controls->addStretch();
        controls->addWidget(addButton);
        controls->addWidget(cancelButton);

        layout->addWidget(searchField_);
        layout->addWidget(table_);
        layout->addLayout(controls);

        rebuildTable();

        iconTimer_ = new QTimer(this);
        iconTimer_->setInterval(1);
        connect(iconTimer_, &QTimer::timeout, this, &TechnologySelectionDialog::loadNextIconBatch);

        connect(searchField_, &QLineEdit::textChanged, this, &TechnologySelectionDialog::filterList);
        connect(addButton, &QPushButton::clicked, this, [this]() {
            int row = currentSelectedRow();
            if (row < 0) {
                return;
            }
            QTableWidgetItem *item = table_->item(row, 2);
            if (!item) {
                return;
            }
            selectedId_ = item->data(Qt::UserRole).toString();
            accept();
        });
        connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
        connect(table_, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem *) {
            int row = currentSelectedRow();
            if (row < 0) {
                return;
            }
            QTableWidgetItem *item = table_->item(row, 2);
            if (!item) {
                return;
            }
            selectedId_ = item->data(Qt::UserRole).toString();
            accept();
        });

        if (table_->rowCount() > 0) {
            table_->selectRow(0);
        }
    }

    QString selectedId() const
    {
        return selectedId_;
    }

protected:
    void showEvent(QShowEvent *event) override
    {
        QDialog::showEvent(event);
        startIconLoading();
    }

private:
    void filterList(const QString &text)
    {
        const QString needle = text.trimmed().toLower();
        for (int row = 0; row < table_->rowCount(); ++row) {
            QTableWidgetItem *nameItem = table_->item(row, 0);
            QTableWidgetItem *idItem = table_->item(row, 2);
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

    void startIconLoading()
    {
        if (!iconTimer_ || iconTimer_->isActive()) {
            return;
        }
        iconLoadIndex_ = 0;
        iconTimer_->start();
    }

    void loadNextIconBatch()
    {
        if (!table_) {
            iconTimer_->stop();
            return;
        }

        const int count = table_->rowCount();
        const int batchSize = 40;
        const int end = qMin(iconLoadIndex_ + batchSize, count);

        for (int i = iconLoadIndex_; i < end; ++i) {
            QTableWidgetItem *nameItem = table_->item(i, 0);
            QTableWidgetItem *idItem = table_->item(i, 2);
            if (!nameItem || !idItem) {
                continue;
            }
            QString id = idItem->data(Qt::UserRole).toString();
            if (id.isEmpty()) {
                continue;
            }
            QPixmap icon = IconRegistry::iconForId(id);
            if (!icon.isNull()) {
                nameItem->setIcon(QIcon(icon));
            }
        }

        iconLoadIndex_ = end;
        if (iconLoadIndex_ >= count) {
            iconTimer_->stop();
        }
    }

    void rebuildTable()
    {
        const QHash<QString, QString> &categories = technologyCategories();
        table_->setSortingEnabled(false);
        table_->setRowCount(entries_.size());

        for (int row = 0; row < entries_.size(); ++row) {
            const ItemEntry &entry = entries_.at(row);
            const QString normalized = normalizeIdForLookup(entry.id);
            QString name = entry.displayName.isEmpty() ? normalized : entry.displayName;
            QString category = categories.value(normalized);
            if (category.isEmpty()) {
                category = tr("Unknown");
            }

            auto *nameItem = new QTableWidgetItem(name);
            auto *categoryItem = new QTableWidgetItem(category);
            auto *idItem = new QTableWidgetItem(normalized);
            idItem->setData(Qt::UserRole, entry.id);

            table_->setItem(row, 0, nameItem);
            table_->setItem(row, 1, categoryItem);
            table_->setItem(row, 2, idItem);
        }
        table_->setSortingEnabled(true);
        table_->sortItems(0, Qt::AscendingOrder);
    }

    int currentSelectedRow() const
    {
        QModelIndexList rows = table_->selectionModel()->selectedRows();
        if (rows.isEmpty()) {
            return -1;
        }
        return rows.first().row();
    }

    QList<ItemEntry> entries_;
    QLineEdit *searchField_ = nullptr;
    QTableWidget *table_ = nullptr;
    QTimer *iconTimer_ = nullptr;
    int iconLoadIndex_ = 0;
    QString selectedId_;
};
} // namespace

KnownTechnologyDialog::KnownTechnologyDialog(const QJsonArray &knownTech, QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(820, 540);

    for (const QJsonValue &value : knownTech) {
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

    addButton_ = new QPushButton(tr("Add Technology"), this);
    removeButton_ = new QPushButton(tr("Remove Selected"), this);
    addButton_->setFixedSize(140, 26);
    removeButton_->setFixedSize(140, 26);
    searchRow->addWidget(searchField_);
    searchRow->addWidget(addButton_);
    searchRow->addWidget(removeButton_);
    searchRow->addStretch();
    layout->addLayout(searchRow);
    layout->addWidget(table_);

    connect(addButton_, &QPushButton::clicked, this, &KnownTechnologyDialog::addTechnology);
    connect(removeButton_, &QPushButton::clicked, this, &KnownTechnologyDialog::removeSelected);
    connect(table_->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this]() {
        refreshRemoveEnabled();
    });
    connect(searchField_, &QLineEdit::textChanged, this, &KnownTechnologyDialog::filterTable);

    allEntries_ = ItemCatalog::itemsForTypes({ItemType::Technology});
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

QJsonArray KnownTechnologyDialog::updatedTech() const
{
    QJsonArray result;
    for (const QString &id : knownIds_) {
        result.append(id);
    }
    return result;
}

bool KnownTechnologyDialog::hasChanges() const
{
    return hasChanges_;
}

void KnownTechnologyDialog::rebuildTable()
{
    const QHash<QString, QString> &categories = technologyCategories();
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

void KnownTechnologyDialog::refreshRemoveEnabled()
{
    if (!removeButton_) {
        return;
    }
    removeButton_->setEnabled(!table_->selectionModel()->selectedRows().isEmpty());
}

void KnownTechnologyDialog::addTechnology()
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
                                 tr("Select technologies that are not known yet."));
        return;
    }

    QString confirmMessage;
    if (addNames.size() == 1) {
        confirmMessage = tr("Add %1 to known technologies?").arg(addNames.first());
    } else {
        QStringList preview = addNames.mid(0, 6);
        QString details = preview.join("\n");
        if (addNames.size() > preview.size()) {
            details += tr("\n...and %1 more").arg(addNames.size() - preview.size());
        }
        confirmMessage = tr("Add %1 technologies?\n%2").arg(addNames.size()).arg(details);
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
    emit knownTechChanged(updatedTech());
}

void KnownTechnologyDialog::removeSelected()
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
                                 tr("Select known technologies to remove."));
        return;
    }

    QString confirmMessage;
    if (removeNames.size() == 1) {
        confirmMessage = tr("Remove %1 from known technologies?").arg(removeNames.first());
    } else {
        QStringList preview = removeNames.mid(0, 6);
        QString details = preview.join("\n");
        if (removeNames.size() > preview.size()) {
            details += tr("\n...and %1 more").arg(removeNames.size() - preview.size());
        }
        confirmMessage = tr("Remove %1 technologies?\n%2").arg(removeNames.size()).arg(details);
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
    emit knownTechChanged(updatedTech());
}

QString KnownTechnologyDialog::normalizedId(const QString &value) const
{
    return normalizeIdForLookup(value);
}

void KnownTechnologyDialog::filterTable(const QString &text)
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
