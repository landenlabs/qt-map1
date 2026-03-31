#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QMetaObject>
#include <cstdio>

#include "Logger.h"

// ── Qt message handler ────────────────────────────────────────────────────────
// Routes qInfo() and qWarning() to both stdout and the in-app log panel.
// Debug messages go to stdout only (too noisy for the panel).

static Logger *gLogger = nullptr;

static void qtMessageHandler(QtMsgType type,
                             const QMessageLogContext & /*ctx*/,
                             const QString &msg)
{
    fprintf(stdout, "%s\n", qPrintable(msg));
    fflush(stdout);

    if (gLogger && (type == QtInfoMsg || type == QtWarningMsg)) {
        // QueuedConnection: safe to call from any thread; posts to the event loop.
        QMetaObject::invokeMethod(gLogger, "appendSilent",
                                  Qt::QueuedConnection,
                                  Q_ARG(QString, msg));
    }
}
#include "OverlayItem.h"
#include "LayerManager.h"
#include "GridManager.h"


static const char *kAppVersion = PROJECT_VERSION;
static const char *kBuildDate  = __DATE__ " " __TIME__;
static const char *kAppUrl     = "https://github.com/landenlabs/qt-map1";
static const char *kSunApiKey  = SUN_API_KEY;  // Weather Company layer tile key

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationVersion(QLatin1String(kAppVersion));

    // Use the Basic style so that custom `background` on Button delegates works.
    // The native macOS style does not support background customization and
    // emits a warning for every customized button.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    qmlRegisterType<OverlayItem>("MapApp", 1, 0, "OverlayItem");

    QQmlApplicationEngine engine;

    engine.rootContext()->setContextProperty("appVersion", QLatin1String(kAppVersion));
    engine.rootContext()->setContextProperty("buildDate",  QLatin1String(kBuildDate));
    engine.rootContext()->setContextProperty("appUrl",     QLatin1String(kAppUrl));

    Logger logger;
    gLogger = &logger;
    qInstallMessageHandler(qtMessageHandler);
    engine.rootContext()->setContextProperty("appLogger", &logger);

    // LayerManager reads layers.json, owns the two-stage network fetch,
    // and is exposed to QML as layerManager.
    LayerManager layerManager(QLatin1String(LAYERS_FILE_PATH),
                              QLatin1String(kSunApiKey));
    engine.rootContext()->setContextProperty("layerManager", &layerManager);

    // GridManager reads grids.json and is exposed to QML as gridManager.
    GridManager gridManager(QLatin1String(GRIDS_FILE_PATH),
                            QLatin1String(kSunApiKey));
    engine.rootContext()->setContextProperty("gridManager", &gridManager);

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
