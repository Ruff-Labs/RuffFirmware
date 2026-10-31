#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include "backend/virtual_remote.hpp"
#include "backend/backend.hpp"

int main(int argc, char *argv[])
{
    Backend backend;
    setContextProperty("backend", &backend);
    QGuiApplication app(argc, argv);

    // "Basic" is the most customizable Controls style, so your Figma
    // styling isn't fighting a platform look (macOS/Material/etc).
    QQuickStyle::setStyle("Basic");

    // Judge remote buttons on screen (RemoteScreen.qml) until the RF receiver is wired up.
    // Declared before the engine so it outlives the QML that uses it.
    VirtualRemote virtualRemote;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("virtualRemote", &virtualRemote);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(-1); },
                     Qt::QueuedConnection);

    engine.loadFromModule("BoardGui", "Main");
    return app.exec();
}
