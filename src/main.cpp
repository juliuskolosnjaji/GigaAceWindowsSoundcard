#include "version.h"
#include <QApplication>
#include <QStandardPaths>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QStyleFactory>
#include <QtGlobal>
#include <QSysInfo>
#include <QMutex>
#include <cstdio>
#include "mainwindow.h"

static QFile*       g_log_file   = nullptr;
static QTextStream* g_log_stream = nullptr;
static QMutex       g_log_mutex;

static void messageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
    QMutexLocker lock(&g_log_mutex);
    if (!g_log_stream) return;

    const char* level = "DEBUG";
    switch (type) {
    case QtWarningMsg:  level = "WARN";  break;
    case QtCriticalMsg: level = "ERROR"; break;
    case QtFatalMsg:    level = "FATAL"; break;
    default:            level = "DEBUG"; break;
    }

    QString line = QString("[%1] [%2] %3")
        .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz"))
        .arg(level)
        .arg(msg);

    if (ctx.file)
        line += QString(" (%1:%2)").arg(ctx.file).arg(ctx.line);

    *g_log_stream << line << "\n";
    g_log_stream->flush();

    if (type == QtFatalMsg)
        abort();
}

static void setupLogging() {
    QString log_dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(log_dir);

    QString log_path = log_dir + "/gigaace.log";

    g_log_file = new QFile(log_path);
    if (!g_log_file->open(QIODevice::Append | QIODevice::Text)) {
        delete g_log_file;
        g_log_file = nullptr;
        return;
    }

    g_log_stream = new QTextStream(g_log_file);
    *g_log_stream << "\n--- GigaACE started "
                  << QDateTime::currentDateTime().toString(Qt::ISODate) << " ---\n";
    g_log_stream->flush();

    qInstallMessageHandler(messageHandler);
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("GigaACE Virtual Sound Card");
    app.setOrganizationName("GigaAce");
    if (QStyleFactory::keys().contains("windowsvista", Qt::CaseInsensitive))
        app.setStyle("windowsvista");
    else if (QStyleFactory::keys().contains("windows", Qt::CaseInsensitive))
        app.setStyle("windows");

    setupLogging();
    qInfo() << "GigaACE Virtual Sound Card" << GIGAACE_VERSION_STR
            << "build" << GIGAACE_BUILD_DATE << GIGAACE_BUILD_TIME;
    qInfo() << "Qt version:" << QT_VERSION_STR;
    qInfo() << "OS:" << QSysInfo::prettyProductName();
    qInfo() << "Log:" << QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/gigaace.log";

    int result = 0;
    try {
        MainWindow window;
        window.show();
        result = app.exec();
    } catch (const std::exception& e) {
        QString msg = QString("Unhandled exception: %1").arg(e.what());
        qCritical() << msg;
        QMessageBox::critical(nullptr, "GigaACE Fatal Error", msg +
            "\n\nLog: " + QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/gigaace.log");
        result = 1;
    } catch (...) {
        qCritical() << "Unknown unhandled exception";
        QMessageBox::critical(nullptr, "GigaACE Fatal Error",
            "An unknown error occurred.\n\nLog: " +
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/gigaace.log");
        result = 1;
    }

    qInfo() << "Application exiting with code" << result;

    if (g_log_stream) { delete g_log_stream; g_log_stream = nullptr; }
    if (g_log_file)   { g_log_file->close(); delete g_log_file; g_log_file = nullptr; }

    return result;
}
