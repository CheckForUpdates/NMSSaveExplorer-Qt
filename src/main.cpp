#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QLibrary>
#include <QResource>
#include <QStandardPaths>
#include <QTextStream>

#include <memory>
#include <vector>

#include "MainWindow.h"
#include "core/SaveGameLocator.h"

namespace {
QFile *g_logFile = nullptr;
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
std::vector<std::unique_ptr<QLibrary>> g_resourceLibs;
#endif

QString logLevelText(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return QStringLiteral("DEBUG");
    case QtInfoMsg:
        return QStringLiteral("INFO");
    case QtWarningMsg:
        return QStringLiteral("WARN");
    case QtCriticalMsg:
        return QStringLiteral("CRIT");
    case QtFatalMsg:
        return QStringLiteral("FATAL");
    }
    return QStringLiteral("LOG");
}

void logMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    if (!g_logFile) {
        const QString logDir = QStringLiteral("~/Downloads/logs");
        (void)QDir().mkpath(logDir);
        g_logFile = new QFile(QDir(logDir).filePath("nmssaveexplorer.log"));
        (void)g_logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    }

    QString timestamp = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    QString contextInfo;
    if (context.file && context.line > 0) {
        contextInfo = QString("%1:%2").arg(context.file).arg(context.line);
    }
    QString line = QString("%1 [%2] %3 %4\n")
                       .arg(timestamp, logLevelText(type), contextInfo, msg);

    if (g_logFile && g_logFile->isOpen()) {
        QTextStream stream(g_logFile);
        stream << line;
        stream.flush();
    } else {
        fprintf(stderr, "%s", line.toLocal8Bit().constData());
        fflush(stderr);
    }

    if (type == QtFatalMsg) {
        abort();
    }
}

#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
QStringList resourceSearchDirs()
{
    const QString baseDir = QCoreApplication::applicationDirPath();
    QStringList dirs = {baseDir};
#ifdef Q_OS_LINUX
    QDir base(baseDir);
    dirs << base.filePath(QStringLiteral("../lib"));
    dirs << base.filePath(QStringLiteral("../lib64"));
#endif
    return dirs;
}

QStringList findResourceLibraries(const QStringList &searchDirs)
{
    QStringList libs;
#ifdef Q_OS_WIN
    const QStringList filters = {QStringLiteral("NMSResources*.dll")};
#else
    const QStringList filters = {QStringLiteral("libNMSResources*.so*")};
#endif
    for (const QString &dirPath : searchDirs) {
        QDir dir(dirPath);
        const QStringList matches = dir.entryList(filters, QDir::Files);
        for (const QString &name : matches) {
            libs << dir.filePath(name);
        }
    }
    return libs;
}

QString findResourceIconsRcc(const QStringList &searchDirs)
{
    const QString rccName = QStringLiteral("resources_icons.rcc");
    for (const QString &dirPath : searchDirs) {
        QDir dir(dirPath);
        const QString candidate = dir.filePath(rccName);
        if (QFile::exists(candidate)) {
            return candidate;
        }
    }
    return QString();
}

void loadResourceLibraries()
{
    const QStringList searchDirs = resourceSearchDirs();
    const QStringList libs = findResourceLibraries(searchDirs);
    const QString iconsRccPath = findResourceIconsRcc(searchDirs);

    if (libs.isEmpty() && iconsRccPath.isEmpty()) {
        return;
    }
    if (libs.isEmpty()) {
        qWarning() << "No resource libraries found in" << searchDirs;
    }
    for (const QString &libPath : libs) {
        auto lib = std::make_unique<QLibrary>(libPath);
        if (!lib->load()) {
            qWarning() << "Failed to load resource library" << libPath << lib->errorString();
            continue;
        }
        qInfo() << "Loaded resource library" << libPath;
        g_resourceLibs.push_back(std::move(lib));
    }

    if (iconsRccPath.isEmpty()) {
        qWarning() << "Icon RCC not found in" << searchDirs;
        return;
    }
    if (!QResource::registerResource(iconsRccPath)) {
        qWarning() << "Failed to register icon RCC:" << iconsRccPath;
        return;
    }
    qInfo() << "Registered icon RCC:" << iconsRccPath;
}
#endif
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    {
        QPalette palette = app.palette();
        palette.setColor(QPalette::PlaceholderText, QColor(138, 138, 138));
        app.setPalette(palette);
    }
    qInstallMessageHandler(logMessageHandler);
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    loadResourceLibraries();
#endif
    qInfo() << "NMSSaveExplorer-Qt starting.";
    qRegisterMetaType<SaveSlot>();
    MainWindow window;
    window.show();
    return app.exec();
}
