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
    QString otherSavePathForSelection() const;
    void setSaveEnabled(bool enabled);
    void setLoadedSavePath(const QString &path);
    void setSyncState(bool pending, bool applied);

signals:
    void refreshRequested();
    void browseRequested();
    void loadSaveRequested();
    void openJsonRequested();
    void openInventoryRequested();
    void materialLookupRequested();
    void saveChangesRequested();
    void syncOtherSaveRequested();
    void undoSyncRequested();

private:
    void updateButtonState();
    void updateSaveFilesTable(const SaveSlot &slot);
    void setRowBold(QTableWidget *table, int row, bool bold);
    void updateSlotSelection(int row);
    void updateSaveSelection(int row);
    bool hasOtherSaveForSelection() const;

    QLabel *headingLabel_ = nullptr;
    QTableWidget *slotTable_ = nullptr;
    QTableWidget *saveTable_ = nullptr;
    QPushButton *loadButton_ = nullptr;
    QPushButton *syncButton_ = nullptr;
    QPushButton *undoSyncButton_ = nullptr;
    QPushButton *saveChangesButton_ = nullptr;
    QString loadedSavePath_;
    QString selectedSavePath_;
    QList<SaveSlot> saveSlots_;
    int selectedSlotRow_ = -1;
    int selectedSaveRow_ = -1;
    bool syncPending_ = false;
    bool syncApplied_ = false;
};
