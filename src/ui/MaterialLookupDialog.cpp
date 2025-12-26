#include "ui/MaterialLookupDialog.h"

#include "registry/IconRegistry.h"
#include "registry/ItemDefinitionRegistry.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPixmap>
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

    connect(searchField_, &QLineEdit::textChanged, this, &MaterialLookupDialog::filterList);
    connect(listWidget_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        QString id = item->data(Qt::UserRole).toString();
        QString name = item->text();
        showDetail(id, name);
    });
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

    dialog.resize(180, 200);
    dialog.exec();
}
