#pragma once

#include <QDialog>
#include <QTimer>

#include "registry/ItemCatalog.h"

class QListWidget;
class QLineEdit;

struct ItemSelectionResult {
    ItemEntry entry;
    int amount = 1;
};

class ItemSelectionDialog : public QDialog
{
    Q_OBJECT

public:
    ItemSelectionDialog(const QList<ItemEntry> &entries, QWidget *parent = nullptr);

    bool hasSelection() const;
    ItemSelectionResult selection() const;

private:
    void showEvent(QShowEvent *event) override;
    void filterList(const QString &text);
    void updateAmountPlaceholder();
    bool isValidAmount() const;
    void startIconLoading();
    void loadNextIconBatch();

    QListWidget *listWidget_ = nullptr;
    QLineEdit *searchField_ = nullptr;
    QLineEdit *amountField_ = nullptr;
    QList<ItemEntry> entries_;
    ItemSelectionResult selection_;
    QTimer *iconTimer_ = nullptr;
    int iconLoadIndex_ = 0;
};
