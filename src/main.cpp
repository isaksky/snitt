#include <QApplication>
#include <QLocalServer>
#include <QLockFile>
#include <QDir>
#include <QStandardPaths>
#include <QThread>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>

#include "backend.h"
#include "editorcanvas.h"
#include "appservice.h"
#include "appsettings.h"
#include "playbackclock.h"
#include <QFileInfo>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("snitt");
    app.setApplicationDisplayName("Snitt");
    app.setOrganizationName("snitt");
    app.setQuitOnLastWindowClosed(false);

#ifdef Q_OS_MACOS
    // The packaged playback plugin uses an LGPL-only, software FFmpeg decoder.
    // Select it before QML constructs MediaPlayer; development builds without
    // the private plugin keep Qt's normal backend.
    const QString ffmpegPlugin = QDir(QCoreApplication::applicationDirPath())
        .filePath("../PlugIns/multimedia/libffmpegmediaplugin.dylib");
    if (QFileInfo::exists(ffmpegPlugin)) {
        if (!qEnvironmentVariableIsSet("QT_MEDIA_BACKEND"))
            qputenv("QT_MEDIA_BACKEND", "ffmpeg");
        if (!qEnvironmentVariableIsSet("QT_FFMPEG_DECODING_HW_DEVICE_TYPES"))
            qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", ",");
    }
#endif

    QStringList args = app.arguments().mid(1);
    for (QString &arg : args)
        if (!arg.startsWith('-')) arg = QFileInfo(arg).absoluteFilePath();
    if (AppService::sendCommand(args)) return 0;
    if (args.contains("--quit")) return 0;
    QLockFile lock(QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                      .filePath(AppService::serverName() + ".lock"));
    if (!lock.tryLock(0)) {
        for (int i = 0; i < 10; ++i) {
            QThread::msleep(100);
            if (AppService::sendCommand(args)) return 0;
        }
        qWarning("Another Snitt instance is starting or is unresponsive.");
        return 1;
    }
    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    QLocalServer::removeServer(AppService::serverName());
    if (!server.listen(AppService::serverName())) return 1;

    QQuickStyle::setStyle("Material");
    qmlRegisterType<EditorCanvas>("Snitt", 1, 0, "EditorCanvas");
    qmlRegisterType<PlaybackClock>("Snitt", 1, 0, "PlaybackClock");

    AppSettings settings;
    Backend backend(&settings);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("appSettings", &settings);
    QString imagePath;
    for (const auto &arg : args) if (!arg.startsWith('-')) { imagePath = arg; break; }
    engine.rootContext()->setContextProperty("initialImage",
        imagePath.isEmpty() ? QUrl() : QUrl::fromLocalFile(imagePath));
    engine.rootContext()->setContextProperty("startInBackground", args.contains("--background"));
    engine.rootContext()->setContextProperty("showOnStart", args.contains("--show"));
    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    AppService service(&server, engine.rootObjects().first(), &settings);

    return app.exec();
}
