#pragma once

#include <QJsonArray>
#include <QStringList>
#include <QWidget>

#include "registry/ItemCatalog.h"

class QTableWidget;
class QPushButton;
class QLineEdit;
class LoadingOverlay;

class KnownTechnologyDialog : public QWidget
{
    Q_OBJECT

public:
    explicit KnownTechnologyDialog(const QJsonArray &knownTech, QWidget *parent = nullptr);
    QJsonArray updatedTech() const;
    bool hasChanges() const;

signals:
    void knownTechChanged(const QJsonArray &updated);

private:
    void rebuildTable();
    void refreshRemoveEnabled();
    void addTechnology();
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
