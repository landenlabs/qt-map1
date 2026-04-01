#include <QDir>
#include <QGuiApplication>
#include <QMetaObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <cstdio>

#include "AppSettings.h"
#include "GridManager.h"
#include "LayerManager.h"
#include "Logger.h"
#include "OverlayItem.h"

// ── Qt message handler ────────────────────────────────────────────────────────
// Routes qInfo() and qWarning() to both stdout and the in-app log panel.

static Logger *gLogger = nullptr;

static void qtMessageHandler(QtMsgType type, const QMessageLogContext & /*ctx*/, const QString &msg) {
    fprintf(stdout, "%s\n", qPrintable(msg));
    fflush(stdout);

    if (gLogger && (type == QtInfoMsg || type == QtWarningMsg)) {
        QMetaObject::invokeMethod(gLogger, "appendSilent", Qt::QueuedConnection, Q_ARG(QString, msg));
    }
}

static const char *kAppVersion = PROJECT_VERSION;
static const char *kBuildDate = __DATE__ " " __TIME__;
static const char *kAppUrl = "https://github.com/landenlabs/qt-map1";
static const char *kSunApiKey = SUN_API_KEY;
static constexpr qint64 kBuildUnixTime = BUILD_UNIX_TIME;
static constexpr int kExpiryDays = SUN_API_KEY_EXPIRY_DAYS;

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationVersion(QLatin1String(kAppVersion));
    app.setOrganizationName(QStringLiteral("qt-map1"));

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    qmlRegisterType<OverlayItem>("MapApp", 1, 0, "OverlayItem");

    QQmlApplicationEngine engine;

    engine.rootContext()->setContextProperty("appVersion", QLatin1String(kAppVersion));
    engine.rootContext()->setContextProperty("buildDate", QLatin1String(kBuildDate));
    engine.rootContext()->setContextProperty("appUrl", QLatin1String(kAppUrl));
    engine.rootContext()->setContextProperty("homePath", QDir::homePath());

    Logger logger;
    gLogger = &logger;
    qInstallMessageHandler(qtMessageHandler);
    engine.rootContext()->setContextProperty("appLogger", &logger);

    // AppSettings – persists user preferences across sessions via QSettings.
    // Passes built-in key metadata so expiry can be computed at construction.
    AppSettings appSettings(QLatin1String(kSunApiKey), kExpiryDays, kBuildUnixTime);
    engine.rootContext()->setContextProperty("appSettings", &appSettings);

    // LayerManager and GridManager load from compiled-in resources then apply
    // any saved search paths so external overrides are active from first run.
    // Use effectiveApiKey() so a previously saved user key takes effect immediately.
    const QString apiKey = appSettings.effectiveApiKey();
    const QStringList initialPaths = appSettings.searchPaths();

    LayerManager layerManager(apiKey);
    layerManager.reload(initialPaths);
    engine.rootContext()->setContextProperty("layerManager", &layerManager);

    GridManager gridManager(apiKey);
    gridManager.reload(initialPaths);
    engine.rootContext()->setContextProperty("gridManager", &gridManager);

    // When the user adds/removes search paths in the About dialog, reload both
    // managers so new or replaced entries appear immediately.
    QObject::connect(&appSettings, &AppSettings::searchPathsChanged, [&layerManager, &gridManager](const QStringList &paths) {
        layerManager.reload(paths);
        gridManager.reload(paths);
    });

    // When the user saves or clears their API key, push the new effective key
    // to both managers.  They use it on the next enableGrid()/enableLayer() call.
    QObject::connect(&appSettings, &AppSettings::sunApiKeyChanged, [&layerManager, &gridManager, &appSettings](const QString &) {
        const QString key = appSettings.effectiveApiKey();
        layerManager.setApiKey(key);
        gridManager.setApiKey(key);
    });

    const QUrl url(QStringLiteral("qrc:/qt/qml/MapApp/main.qml"));
    QObject::connect(
            &engine,
            &QQmlApplicationEngine::objectCreated,
            &app,
            [url](QObject *obj, const QUrl &objUrl) {
                if (!obj && url == objUrl)
                    QCoreApplication::exit(-1);
            },
            Qt::QueuedConnection
    );

    engine.load(url);
    return app.exec();
}
