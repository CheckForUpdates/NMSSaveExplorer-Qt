#include "inventory/ItemSelectionDialog.h"

#include "registry/IconRegistry.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QShowEvent>
#include <QVBoxLayout>

ItemSelectionDialog::ItemSelectionDialog(const QList<ItemEntry> &entries, QWidget *parent)
    : QDialog(parent), entries_(entries)
{
    setWindowTitle(tr("Find Item"));
    setMinimumSize(520, 520);

    auto *layout = new QVBoxLayout(this);
    searchField_ = new QLineEdit(this);
    searchField_->setPlaceholderText(tr("Search by name or ID..."));

    listWidget_ = new QListWidget(this);
    listWidget_->setSelectionMode(QAbstractItemView::SingleSelection);

    amountField_ = new QLineEdit(this);
    amountField_->setPlaceholderText(tr("Amount"));
    amountField_->setMaximumWidth(120);

    auto *controls = new QHBoxLayout();
    auto *addButton = new QPushButton(tr("Add"), this);
    auto *cancelButton = new QPushButton(tr("Cancel"), this);
    controls->addWidget(new QLabel(tr("Amount:"), this));
    controls->addWidget(amountField_);
    controls->addStretch();
    controls->addWidget(addButton);
    controls->addWidget(cancelButton);

    layout->addWidget(searchField_);
    layout->addWidget(listWidget_);
    layout->addLayout(controls);

    for (const ItemEntry &entry : entries_) {
        QString label = entry.displayName.isEmpty() ? entry.id : QString("%1 (%2)").arg(entry.displayName, entry.id);
        auto *item = new QListWidgetItem(label, listWidget_);
        item->setData(Qt::UserRole, entry.id);
    }

    iconTimer_ = new QTimer(this);
    iconTimer_->setInterval(1);
    connect(iconTimer_, &QTimer::timeout, this, &ItemSelectionDialog::loadNextIconBatch);

    connect(searchField_, &QLineEdit::textChanged, this, &ItemSelectionDialog::filterList);
    connect(listWidget_, &QListWidget::currentRowChanged, this, [this](int) {
        updateAmountPlaceholder();
    });
    connect(addButton, &QPushButton::clicked, this, [this]() {
        if (!isValidAmount()) {
            return;
        }
        int row = listWidget_->currentRow();
        if (row < 0 || row >= entries_.size()) {
            return;
        }
        selection_.entry = entries_.at(row);
        selection_.amount = amountField_->text().toInt();
        accept();
    });
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(listWidget_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) {
        if (!isValidAmount()) {
            return;
        }
        int row = listWidget_->currentRow();
        if (row < 0 || row >= entries_.size()) {
            return;
        }
        selection_.entry = entries_.at(row);
        selection_.amount = amountField_->text().toInt();
        accept();
    });

    if (!entries_.isEmpty()) {
        listWidget_->setCurrentRow(0);
    }
}

void ItemSelectionDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    startIconLoading();
}

bool ItemSelectionDialog::hasSelection() const
{
    return selection_.amount > 0 && !selection_.entry.id.isEmpty();
}

ItemSelectionResult ItemSelectionDialog::selection() const
{
    return selection_;
}

void ItemSelectionDialog::filterList(const QString &text)
{
    QString query = text.trimmed().toLower();
    for (int i = 0; i < listWidget_->count(); ++i) {
        QListWidgetItem *item = listWidget_->item(i);
        bool match = query.isEmpty() || item->text().toLower().contains(query);
        item->setHidden(!match);
    }
}

void ItemSelectionDialog::updateAmountPlaceholder()
{
    int row = listWidget_->currentRow();
    if (row < 0 || row >= entries_.size()) {
        amountField_->setPlaceholderText(tr("Amount"));
        return;
    }
    const ItemEntry &entry = entries_.at(row);
    int suggested = 1;
    switch (entry.type) {
    case ItemType::Substance:
        suggested = 250;
        break;
    case ItemType::Product:
    case ItemType::Technology:
        suggested = 1;
        break;
    default:
        suggested = 1;
        break;
    }
    if (entry.maxStack > 0) {
        suggested = qMin(entry.maxStack, suggested);
    }
    amountField_->setPlaceholderText(tr("Max %1").arg(entry.maxStack));
    if (amountField_->text().isEmpty()) {
        amountField_->setText(QString::number(suggested));
    }
}

bool ItemSelectionDialog::isValidAmount() const
{
    bool ok = false;
    int value = amountField_->text().toInt(&ok);
    return ok && value > 0;
}

void ItemSelectionDialog::startIconLoading()
{
    if (!iconTimer_) {
        return;
    }
    if (iconTimer_->isActive()) {
        return;
    }
    iconLoadIndex_ = 0;
    iconTimer_->start();
}

void ItemSelectionDialog::loadNextIconBatch()
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
