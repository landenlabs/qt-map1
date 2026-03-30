#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include "OverlayItem.h"

// Version is injected by CMake via the project() VERSION field.
// Compile date/time come from the compiler's built-in macros.
static const char *kAppVersion   = PROJECT_VERSION;  // defined in CMakeLists target_compile_definitions
static const char *kBuildDate    = __DATE__ " " __TIME__;
static const char *kAppUrl       = "https://github.com/landenlabs/qt-map1";

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationVersion(QLatin1String(kAppVersion));

    qmlRegisterType<OverlayItem>("MapApp", 1, 0, "OverlayItem");

    QQmlApplicationEngine engine;

    // Expose build-time constants to QML as global context properties
    engine.rootContext()->setContextProperty("appVersion",  QLatin1String(kAppVersion));
    engine.rootContext()->setContextProperty("buildDate",   QLatin1String(kBuildDate));
    engine.rootContext()->setContextProperty("appUrl",      QLatin1String(kAppUrl));

    const QUrl url(QStringLiteral("qrc:/qt/qml/MapApp/main.qml"));
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreated,
        &app, [url](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);
        },
        Qt::QueuedConnection);

    engine.load(url);
    return app.exec();
}
