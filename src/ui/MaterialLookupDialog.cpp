#include "ui/MaterialLookupDialog.h"

#include "registry/IconRegistry.h"
#include "registry/ItemCatalog.h"
#include "registry/ItemDefinitionRegistry.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QIcon>
#include <QPixmap>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>

MaterialLookupDialog::MaterialLookupDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Material Lookup"));
    setMinimumSize(420, 520);

    auto *layout = new QVBoxLayout(this);
    searchField_ = new QLineEdit(this);
    searchField_->setPlaceholderText(tr("Search by name or ID..."));
    listWidget_ = new QListWidget(this);

    layout->addWidget(searchField_);
    layout->addWidget(listWidget_);

    populateList();

    iconTimer_ = new QTimer(this);
    iconTimer_->setInterval(1);
    connect(iconTimer_, &QTimer::timeout, this, &MaterialLookupDialog::loadNextIconBatch);

    connect(searchField_, &QLineEdit::textChanged, this, &MaterialLookupDialog::filterList);
    connect(listWidget_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        QString id = item->data(Qt::UserRole).toString();
        QString name = item->text();
        showDetail(id, name);
    });
}

void MaterialLookupDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    startIconLoading();
}

void MaterialLookupDialog::populateList()
{
    listWidget_->clear();
    QHash<QString, ItemDefinition> defs = ItemDefinitionRegistry::allDefinitions();
    QList<QString> keys = defs.keys();
    std::sort(keys.begin(), keys.end(), [&defs](const QString &a, const QString &b) {
        return defs.value(a).name.toLower() < defs.value(b).name.toLower();
    });
    for (const QString &key : keys) {
        ItemDefinition def = defs.value(key);
        QString label = def.name.isEmpty() ? key : QString("%1 (%2)").arg(def.name, key);
        auto *item = new QListWidgetItem(label, listWidget_);
        item->setData(Qt::UserRole, key);
    }
}

void MaterialLookupDialog::filterList(const QString &text)
{
    QString needle = text.trimmed().toLower();
    for (int i = 0; i < listWidget_->count(); ++i) {
        QListWidgetItem *item = listWidget_->item(i);
        bool match = needle.isEmpty() || item->text().toLower().contains(needle);
        item->setHidden(!match);
    }
}

void MaterialLookupDialog::showDetail(const QString &id, const QString &name)
{
    QDialog dialog(this, Qt::Dialog | Qt::FramelessWindowHint);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(8, 8, 8, 8);

    QLabel *iconLabel = new QLabel(&dialog);
    QPixmap pixmap = IconRegistry::iconForId(id);
    if (!pixmap.isNull()) {
        iconLabel->setPixmap(pixmap.scaled(150, 150, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }

    QLabel *nameLabel = new QLabel(name, &dialog);
    nameLabel->setAlignment(Qt::AlignCenter);

    layout->addWidget(iconLabel, 0, Qt::AlignCenter);
    layout->addWidget(nameLabel);

    ItemType type = ItemType::Unknown;
    QList<ItemEntry> entries = ItemCatalog::itemsForTypes(
        {ItemType::Substance, ItemType::Product, ItemType::Technology});
    QString normalizedId = id.startsWith('^') ? id.mid(1) : id;
    for (const ItemEntry &entry : entries) {
        if (entry.id.compare(normalizedId, Qt::CaseInsensitive) == 0) {
            type = entry.type;
            break;
        }
    }
    QString typeLabel;
    switch (type) {
    case ItemType::Substance:
        typeLabel = tr("Substance");
        break;
    case ItemType::Product:
        typeLabel = tr("Product");
        break;
    case ItemType::Technology:
        typeLabel = tr("Technology");
        break;
    default:
        break;
    }
    if (!typeLabel.isEmpty()) {
        QLabel *typeLabelWidget = new QLabel(typeLabel, &dialog);
        typeLabelWidget->setAlignment(Qt::AlignCenter);
        layout->addWidget(typeLabelWidget);
    }

    dialog.resize(180, 200);
    dialog.exec();
}

void MaterialLookupDialog::startIconLoading()
{
    if (!iconTimer_ || iconTimer_->isActive()) {
        return;
    }
    iconLoadIndex_ = 0;
    iconTimer_->start();
}

void MaterialLookupDialog::loadNextIconBatch()
{
    if (!listWidget_) {
        iconTimer_->stop();
        return;
    }

    const int count = listWidget_->count();
    const int batchSize = 40;
    int end = qMin(iconLoadIndex_ + batchSize, count);

    for (int i = iconLoadIndex_; i < end; ++i) {
        QListWidgetItem *item = listWidget_->item(i);
        if (!item) {
            continue;
        }
        QString id = item->data(Qt::UserRole).toString();
        if (id.isEmpty()) {
            continue;
        }
        QPixmap icon = IconRegistry::iconForId(id);
        if (!icon.isNull()) {
            item->setIcon(QIcon(icon));
        }
    }

    iconLoadIndex_ = end;
    if (iconLoadIndex_ >= count) {
        iconTimer_->stop();
    }
}
