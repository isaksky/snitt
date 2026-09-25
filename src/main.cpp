#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>

#include "backend.h"
#include "editorcanvas.h"
#include <QFileInfo>

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("xshot");
    app.setOrganizationName("xshot");

    QQuickStyle::setStyle("Material");
    qmlRegisterType<EditorCanvas>("XShot", 1, 0, "EditorCanvas");

    Backend backend;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    const QStringList args = app.arguments();
    engine.rootContext()->setContextProperty("initialImage",
        args.size() > 1 ? QUrl::fromLocalFile(QFileInfo(args.at(1)).absoluteFilePath()) : QUrl());
    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}
