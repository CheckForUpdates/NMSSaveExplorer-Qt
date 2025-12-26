#pragma once

#include <QWidget>
#include "core/SaveGameLocator.h"

class QLabel;
class QPushButton;
class QTableWidget;

class WelcomePage : public QWidget
{
    Q_OBJECT

public:
    explicit WelcomePage(QWidget *parent = nullptr);

    void setSlots(const QList<SaveSlot> &saveSlots);
    SaveSlot selectedSlot() const;
    QString selectedSavePath() const;
    void setSaveEnabled(bool enabled);
    void setLoadedSavePath(const QString &path);

signals:
    void refreshRequested();
    void browseRequested();
    void loadSaveRequested();
    void openJsonRequested();
    void openInventoryRequested();
    void materialLookupRequested();
    void saveChangesRequested();

private:
    void updateButtonState();
    void updateSaveFilesTable(const SaveSlot &slot);
    void setRowBold(QTableWidget *table, int row, bool bold);
    void updateSlotSelection(int row);
    void updateSaveSelection(int row);

    QLabel *headingLabel_ = nullptr;
    QTableWidget *slotTable_ = nullptr;
    QTableWidget *saveTable_ = nullptr;
    QPushButton *loadButton_ = nullptr;
    QPushButton *saveChangesButton_ = nullptr;
    QString loadedSavePath_;
    QString selectedSavePath_;
    QList<SaveSlot> saveSlots_;
    int selectedSlotRow_ = -1;
    int selectedSaveRow_ = -1;
};
