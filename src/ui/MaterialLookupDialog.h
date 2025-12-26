#pragma once

#include <QDialog>

class QListWidget;
class QLineEdit;

class MaterialLookupDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MaterialLookupDialog(QWidget *parent = nullptr);

private:
    void populateList();
    void filterList(const QString &text);
    void showDetail(const QString &id, const QString &name);

    QLineEdit *searchField_ = nullptr;
    QListWidget *listWidget_ = nullptr;
};
