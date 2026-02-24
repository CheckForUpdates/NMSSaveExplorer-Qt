#include <QCoreApplication>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <algorithm>

namespace {
const char *kProductTable = "data/NMS_REALITY_GCPRODUCTTABLE.MXML";
const char *kBasePartProductTable = "data/NMS_BASEPARTPRODUCTS.MXML";
const char *kSubstanceTable = "data/NMS_REALITY_GCSUBSTANCETABLE.MXML";
const char *kTechnologyTable = "data/NMS_REALITY_GCTECHNOLOGYTABLE.MXML";
const char *kDefinitionPath = "localization_map.json";

struct ItemDefinition {
    QString name;
    QString icon;
};

struct ItemEntry {
    QString id;
    QString displayName;
    QString type;
    int maxStack = 0;
    QString icon;
};

QString normalizeId(const QString &value)
{
    return value.trimmed().toUpper();
}

QString normalizeKey(const QString &itemId)
{
    QString key = itemId.startsWith('^') ? itemId.mid(1) : itemId;
    int hashIndex = key.indexOf('#');
    if (hashIndex >= 0) {
        key = key.left(hashIndex);
    }
    return key.toUpper();
}

QHash<QString, ItemDefinition> loadDefinitions(const QString &path)
{
    QHash<QString, ItemDefinition> definitions;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return definitions;
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return definitions;
    }

    QJsonObject root = doc.object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        if (!it.value().isObject()) {
            continue;
        }
        QJsonObject obj = it.value().toObject();
        QString name = obj.value("name").toString();
        QString icon = obj.value("icon").toString();
        if (name.isEmpty() && icon.isEmpty()) {
            continue;
        }
        QString key = it.key().toUpper();
        definitions.insert(key, ItemDefinition{name, icon});
    }
    return definitions;
}

int readIntAttribute(const QString &value, int fallback)
{
    if (value.isEmpty()) {
        return fallback;
    }
    bool ok = false;
    double parsed = value.toDouble(&ok);
    if (!ok) {
        return fallback;
    }
    return static_cast<int>(qRound(parsed));
}

void parseProductTable(const QString &path, QHash<QString, ItemEntry> &entries)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    QDomDocument doc;
    if (!doc.setContent(&file)) {
        return;
    }
    QDomNodeList nodes = doc.elementsByTagName("Property");
    for (int i = 0; i < nodes.count(); ++i) {
        QDomElement element = nodes.at(i).toElement();
        if (element.isNull()) {
            continue;
        }
        if (element.attribute("value") != "GcProductData") {
            continue;
        }
        QString id = normalizeId(element.attribute("_id"));
        if (id.isEmpty()) {
            continue;
        }
        QDomElement child = element.firstChildElement("Property");
        int multiplier = 1;
        while (!child.isNull()) {
            if (child.attribute("name") == "StackMultiplier") {
                multiplier = readIntAttribute(child.attribute("value"), 1);
                break;
            }
            child = child.nextSiblingElement("Property");
        }
        int base = 10;
        ItemEntry entry{ id, QString(), QStringLiteral("Product"), multiplier * base, QString() };
        entries.insert(id, entry);
    }
}

void parseSubstanceTable(const QString &path, QHash<QString, ItemEntry> &entries)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    QDomDocument doc;
    if (!doc.setContent(&file)) {
        return;
    }
    QDomNodeList nodes = doc.elementsByTagName("Property");
    for (int i = 0; i < nodes.count(); ++i) {
        QDomElement element = nodes.at(i).toElement();
        if (element.isNull()) {
            continue;
        }
        if (element.attribute("value") != "GcRealitySubstanceData") {
            continue;
        }
        QString id = normalizeId(element.attribute("_id"));
        if (id.isEmpty()) {
            continue;
        }
        QDomElement child = element.firstChildElement("Property");
        int multiplier = 1;
        while (!child.isNull()) {
            if (child.attribute("name") == "StackMultiplier") {
                multiplier = readIntAttribute(child.attribute("value"), 1);
                break;
            }
            child = child.nextSiblingElement("Property");
        }
        int base = 9999;
        ItemEntry entry{ id, QString(), QStringLiteral("Substance"), multiplier * base, QString() };
        entries.insert(id, entry);
    }
}

void parseTechnologyTable(const QString &path, QHash<QString, ItemEntry> &entries)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    QDomDocument doc;
    if (!doc.setContent(&file)) {
        return;
    }
    QDomNodeList nodes = doc.elementsByTagName("Property");
    for (int i = 0; i < nodes.count(); ++i) {
        QDomElement element = nodes.at(i).toElement();
        if (element.isNull()) {
            continue;
        }
        if (element.attribute("value") != "GcTechnology") {
            continue;
        }
        QString id = normalizeId(element.attribute("_id"));
        if (id.isEmpty()) {
            continue;
        }
        QDomElement child = element.firstChildElement("Property");
        int charge = 1;
        while (!child.isNull()) {
            if (child.attribute("name") == "ChargeAmount") {
                charge = readIntAttribute(child.attribute("value"), 1);
                if (charge <= 0) {
                    charge = 1;
                }
                break;
            }
            child = child.nextSiblingElement("Property");
        }
        ItemEntry entry{ id, QString(), QStringLiteral("Technology"), charge, QString() };
        entries.insert(id, entry);
    }
}

bool writeCatalog(const QString &outputPath, const QList<ItemEntry> &items)
{
    QJsonArray array;
    for (const ItemEntry &entry : items) {
        QJsonObject obj;
        obj.insert("id", entry.id);
        obj.insert("displayName", entry.displayName);
        obj.insert("type", entry.type);
        obj.insert("maxStack", entry.maxStack);
        if (!entry.icon.isEmpty()) {
            obj.insert("icon", entry.icon);
        }
        array.append(obj);
    }

    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    QJsonDocument doc(array);
    file.write(doc.toJson(QJsonDocument::Compact));
    return true;
}

void printUsage(const QString &exeName)
{
    QTextStream out(stderr);
    out << "Usage: " << exeName << " --resources <path> --output <path>\n";
}
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QString resourcesRoot;
    QString outputPath;

    const QStringList args = QCoreApplication::arguments();
    for (int i = 1; i < args.size(); ++i) {
        const QString arg = args.at(i);
        if (arg == "--resources" && i + 1 < args.size()) {
            resourcesRoot = args.at(++i);
        } else if (arg == "--output" && i + 1 < args.size()) {
            outputPath = args.at(++i);
        }
    }

    if (resourcesRoot.isEmpty() || outputPath.isEmpty()) {
        printUsage(QFileInfo(args.value(0)).fileName());
        return 1;
    }

    QDir root(resourcesRoot);
    if (!root.exists()) {
        QTextStream out(stderr);
        out << "Resources root does not exist: " << resourcesRoot << "\n";
        return 1;
    }

    QHash<QString, ItemEntry> entries;
    parseProductTable(root.filePath(kProductTable), entries);
    parseProductTable(root.filePath(kBasePartProductTable), entries);
    parseSubstanceTable(root.filePath(kSubstanceTable), entries);
    parseTechnologyTable(root.filePath(kTechnologyTable), entries);

    QHash<QString, ItemDefinition> definitions = loadDefinitions(root.filePath(kDefinitionPath));
    QList<ItemEntry> items;
    items.reserve(entries.size());
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        ItemEntry entry = it.value();
        QString defKey = normalizeKey(entry.id);
        ItemDefinition def = definitions.value(defKey);
        if (!def.name.isEmpty()) {
            entry.displayName = def.name;
        } else {
            entry.displayName = entry.id;
        }
        entry.icon = def.icon;
        items.append(entry);
    }
    std::sort(items.begin(), items.end(), [](const ItemEntry &a, const ItemEntry &b) {
        return a.displayName.toLower() < b.displayName.toLower();
    });

    if (!writeCatalog(outputPath, items)) {
        QTextStream out(stderr);
        out << "Failed to write catalog to " << outputPath << "\n";
        return 1;
    }

    return 0;
}
