#pragma once

#include <QString>

class ResourceLocator
{
public:
    static QString resourcesRoot();
    static QString resolveResource(const QString &relativePath);

private:
    static QString findResourcesRoot();
};
