#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include "Logger.h"
#include "OverlayItem.h"
#include "LayerManager.h"

static const char *kAppVersion = PROJECT_VERSION;
static const char *kBuildDate  = __DATE__ " " __TIME__;
static const char *kAppUrl     = "https://github.com/landenlabs/qt-map1";
static const char *kTwcApiKey  = TWC_API_KEY;  // Thunderforest base map key
static const char *kSunApiKey  = SUN_API_KEY;  // Weather Company layer tile key

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationVersion(QLatin1String(kAppVersion));

    qmlRegisterType<OverlayItem>("MapApp", 1, 0, "OverlayItem");

    QQmlApplicationEngine engine;

    engine.rootContext()->setContextProperty("appVersion", QLatin1String(kAppVersion));
    engine.rootContext()->setContextProperty("buildDate",  QLatin1String(kBuildDate));
    engine.rootContext()->setContextProperty("appUrl",     QLatin1String(kAppUrl));
    engine.rootContext()->setContextProperty("twcApiKey",  QLatin1String(kTwcApiKey));

    Logger logger;
    engine.rootContext()->setContextProperty("appLogger", &logger);

    // LayerManager reads layers.json, owns the two-stage network fetch,
    // and is exposed to QML as layerManager.
    LayerManager layerManager(QLatin1String(LAYERS_FILE_PATH),
                              QLatin1String(kSunApiKey));
    engine.rootContext()->setContextProperty("layerManager", &layerManager);

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
