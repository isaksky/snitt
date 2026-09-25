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
#include <QFileInfo>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("xshot");
    app.setOrganizationName("xshot");
    app.setQuitOnLastWindowClosed(false);

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
        qWarning("Another xshot instance is starting or is unresponsive.");
        return 1;
    }
    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    QLocalServer::removeServer(AppService::serverName());
    if (!server.listen(AppService::serverName())) return 1;

    QQuickStyle::setStyle("Material");
    qmlRegisterType<EditorCanvas>("XShot", 1, 0, "EditorCanvas");

    Backend backend;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    QString imagePath;
    for (const auto &arg : args) if (!arg.startsWith('-')) { imagePath = arg; break; }
    engine.rootContext()->setContextProperty("initialImage",
        imagePath.isEmpty() ? QUrl() : QUrl::fromLocalFile(imagePath));
    engine.rootContext()->setContextProperty("startInBackground", args.contains("--background"));
    engine.rootContext()->setContextProperty("showOnStart", args.contains("--show"));
    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    AppService service(&server, engine.rootObjects().first());

    return app.exec();
}
