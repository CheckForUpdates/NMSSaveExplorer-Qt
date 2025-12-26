#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QWidget>

class QGridLayout;
class QLabel;

class InventoryGridWidget : public QWidget
{
    Q_OBJECT

public:
    explicit InventoryGridWidget(QWidget *parent = nullptr);

    void setInventory(const QString &title, const QJsonArray &slotsArray,
                      const QJsonArray &validSlots, const QJsonArray &specialSlots = QJsonArray());
    void setCommitHandler(const std::function<void(const QJsonArray &, const QJsonArray &)> &handler);
    QString title() const { return title_; }
    static int preferredGridWidth();
    static int preferredGridHeight(int rows);

    static constexpr int kCellSize = 100;
    static constexpr int kGridWidth = 10;
    static constexpr int kGridSpacing = 1;
    static constexpr int kGridMargin = 10;

signals:
    void statusMessage(const QString &message);

private:
    struct SlotPosition {
        int x = 0;
        int y = 0;
    };
    class InventoryCell;

    class InventoryCell : public QWidget
    {
    public:
        InventoryCell(int x, int y, QWidget *parent = nullptr);
        void setContent(const QJsonObject &item, int iconSize);
        void setSupercharged(bool supercharged);
        void setEmpty();
        SlotPosition position() const { return {x_, y_}; }
        bool isSupercharged() const { return supercharged_; }
        bool isDamaged() const { return damaged_; }

        void setDragEnabled(bool enabled) { dragEnabled_ = enabled; }
        void setDropEnabled(bool enabled) { dropEnabled_ = enabled; }

        QJsonObject currentItem() const { return item_; }
        bool hasItem() const { return !item_.isEmpty(); }
        bool supportsAmount() const;
        QString displayName() const { return displayName_; }

    protected:
        void mousePressEvent(QMouseEvent *event) override;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        void enterEvent(QEnterEvent *event) override;
#else
        void enterEvent(QEvent *event) override;
#endif
        void leaveEvent(QEvent *event) override;
        void dragEnterEvent(QDragEnterEvent *event) override;
        void dragMoveEvent(QDragMoveEvent *event) override;
        void dropEvent(QDropEvent *event) override;
        void paintEvent(QPaintEvent *event) override;


    protected:
        void updateHover(bool show);

        int x_;
        int y_;
        bool dragEnabled_ = false;
        bool dropEnabled_ = false;
        bool supercharged_ = false;
        bool damaged_ = false;
        QJsonObject item_;
        QString displayName_;
        QLabel *amountLabel_ = nullptr;
        QLabel *iconLabel_ = nullptr;
    };

    void showNameOverlay(InventoryCell *cell);
    void hideNameOverlay();

    void rebuild();
    void attachContextMenu(InventoryCell *cell);
    void moveOrSwap(int srcX, int srcY, int dstX, int dstY);
    void changeItemAmount(InventoryCell *cell);
    void maxItemAmount(InventoryCell *cell);
    void deleteItem(InventoryCell *cell);
    void addItem(InventoryCell *cell);
    void toggleSupercharged(InventoryCell *cell);
    void repairItem(InventoryCell *cell);
    void repairAllDamaged();

    QJsonObject findItemAt(int x, int y, int *index = nullptr) const;

    QString title_;
    QJsonArray slots_;
    QJsonArray validSlots_;
    QJsonArray specialSlots_;
    QGridLayout *grid_ = nullptr;
    QLabel *nameOverlay_ = nullptr;
    std::function<void(const QJsonArray &, const QJsonArray &)> commitHandler_;
};
