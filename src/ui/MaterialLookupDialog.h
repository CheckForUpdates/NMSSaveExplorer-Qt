#pragma once

#include <QDialog>

class QListWidget;
class QLineEdit;
class QShowEvent;
class QTimer;

class MaterialLookupDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MaterialLookupDialog(QWidget *parent = nullptr);

protected:
    void showEvent(QShowEvent *event) override;

private:
    void populateList();
    void filterList(const QString &text);
    void showDetail(const QString &id, const QString &name);
    void startIconLoading();
    void loadNextIconBatch();

    QLineEdit *searchField_ = nullptr;
    QListWidget *listWidget_ = nullptr;
    QTimer *iconTimer_ = nullptr;
    int iconLoadIndex_ = 0;
};
