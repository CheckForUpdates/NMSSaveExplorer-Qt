#include "registry/IconRegistry.h"

#include "core/ResourceLocator.h"
#include "registry/ItemDefinitionRegistry.h"

#include <QFileInfo>

QPixmap IconRegistry::iconForId(const QString &itemId)
{
    QString path = iconPathForId(itemId);
    if (path.isEmpty()) {
        return QPixmap();
    }

    QHash<QString, QPixmap> &icons = cache();
    QString key = path.toLower();
    if (icons.contains(key)) {
        return icons.value(key);
    }
    QPixmap pixmap(path);
    if (!pixmap.isNull()) {
        icons.insert(key, pixmap);
    }
    return pixmap;
}

QString IconRegistry::iconPathForId(const QString &itemId)
{
    ItemDefinition def = ItemDefinitionRegistry::definitionForId(itemId);
    if (def.icon.isEmpty()) {
        return QString();
    }
    return ResourceLocator::resolveResource(QString("icons/%1").arg(def.icon));
}

QHash<QString, QPixmap> &IconRegistry::cache()
{
    static QHash<QString, QPixmap> s_cache;
    return s_cache;
}
