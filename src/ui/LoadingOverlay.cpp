#include "ui/LoadingOverlay.h"

#include <QPainter>
#include <QPaintEvent>
#include <QGuiApplication>

LoadingOverlay::LoadingOverlay(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setAttribute(Qt::WA_NoSystemBackground);
    
    if (parent) {
        parent->installEventFilter(this);
    }
    
    hide();
}

void LoadingOverlay::setMessage(const QString &message)
{
    message_ = message;
    update();
}

void LoadingOverlay::showMessage(const QString &message)
{
    message_ = message;
    show();
    raise();
}

void LoadingOverlay::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    painter.fillRect(rect(), QColor(0, 0, 0, 150));

    if (!message_.isEmpty()) {
        painter.setPen(Qt::white);
        QFont font = painter.font();
        font.setPointSize(12);
        font.setBold(true);
        painter.setFont(font);

        painter.drawText(rect(), Qt::AlignCenter, message_);
    }
}

bool LoadingOverlay::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == parent() && event->type() == QEvent::Resize) {
        resize(static_cast<QWidget*>(parent())->size());
    }
    return QWidget::eventFilter(obj, event);
}

void LoadingOverlay::resizeEvent(QResizeEvent *event)
{
    Q_UNUSED(event);
    update();
}
