#pragma once

#include <QWidget>
class LoadingOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit LoadingOverlay(QWidget *parent = nullptr);

    void setMessage(const QString &message);
    void showMessage(const QString &message);

protected:
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    QString message_;
};
