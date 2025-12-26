#pragma once

#include <QHash>
#include <QPixmap>
#include <QString>

class IconRegistry
{
public:
    static QPixmap iconForId(const QString &itemId);
    static QString iconPathForId(const QString &itemId);

private:
    static QHash<QString, QPixmap> &cache();
};
