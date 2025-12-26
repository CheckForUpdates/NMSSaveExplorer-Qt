#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>

#include "MainWindow.h"
#include "core/SaveGameLocator.h"

namespace {
QFile *g_logFile = nullptr;

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
        const QString logDir = QStringLiteral("/home/lotso/Documents/dev/NMS/NMSSaveExplorer-Qt");
        QDir().mkpath(logDir);
        g_logFile = new QFile(QDir(logDir).filePath("nmssaveexplorer.log"));
        g_logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
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
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    qInstallMessageHandler(logMessageHandler);
    qInfo() << "NMSSaveExplorer-Qt starting.";
    qRegisterMetaType<SaveSlot>();
    MainWindow window;
    window.show();
    return app.exec();
}
