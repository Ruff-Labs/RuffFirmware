#include "backend/backend.hpp"
#include "backend/virtual_remote.hpp"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

#ifndef RUFF_VERSION
#define RUFF_VERSION "dev"   // set by the top-level CMakeLists.txt (qmake builds don't)
#endif

// On the board the GUI draws straight to the display (eglfs/linuxfb) and there's no keyboard, so
// text fields need Qt's on-screen keyboard. RUFF_ONSCREEN_KEYBOARD=1/0 forces it on or off.
static bool want_onscreen_keyboard() {
    if (qEnvironmentVariableIsSet("RUFF_ONSCREEN_KEYBOARD"))
        return qgetenv("RUFF_ONSCREEN_KEYBOARD") != "0";
    const QByteArray platform = qgetenv("QT_QPA_PLATFORM");
    return platform.startsWith("eglfs") || platform.startsWith("linuxfb");
}

// QML gets four context properties:
//   backend           firmware status (backend.hpp)
//   virtualRemote     commands to the firmware (virtual_remote.hpp)
//   appVersion        this GUI's version, shown in Settings + the sidebar
//   onScreenKeyboard  load KeyboardPanel.qml
int main(int argc, char* argv[]) {
    const bool keyboard = want_onscreen_keyboard();
    if (keyboard && !qEnvironmentVariableIsSet("QT_IM_MODULE"))
        qputenv("QT_IM_MODULE", "qtvirtualkeyboard");   // must be set before QGuiApplication

    QGuiApplication app(argc, argv);

    // Basic is the most customizable Controls style, and looks the same on a laptop and the board
    QQuickStyle::setStyle("Basic");

    // before the engine, so they outlive the QML that uses them
    VirtualRemote virtualRemote;
    Backend       backend;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("virtualRemote", &virtualRemote);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("appVersion", QStringLiteral(RUFF_VERSION));
    engine.rootContext()->setContextProperty("onScreenKeyboard", keyboard);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    engine.loadFromModule("BoardGui", "Main");
    return app.exec();
}
