#pragma once

#include <QJsonArray>
#include <QStringList>
#include <QWidget>

#include "registry/ItemCatalog.h"

class QTableWidget;
class QPushButton;
class QLineEdit;
class LoadingOverlay;

class KnownProductDialog : public QWidget
{
    Q_OBJECT

public:
    explicit KnownProductDialog(const QJsonArray &knownProducts, QWidget *parent = nullptr);
    QJsonArray updatedProducts() const;
    bool hasChanges() const;

signals:
    void knownProductsChanged(const QJsonArray &updated);

private:
    void rebuildTable();
    void refreshRemoveEnabled();
    void addProduct();
    void removeSelected();
    void filterTable(const QString &text);
    QString normalizedId(const QString &value) const;
    void showBusyOverlay(const QString &message);
    void hideBusyOverlay();

    QLineEdit *searchField_ = nullptr;
    QTableWidget *table_ = nullptr;
    QPushButton *addButton_ = nullptr;
    QPushButton *removeButton_ = nullptr;
    QStringList knownIds_;
    QList<ItemEntry> allEntries_;
    bool hasChanges_ = false;
    LoadingOverlay *loadingOverlay_ = nullptr;
};
