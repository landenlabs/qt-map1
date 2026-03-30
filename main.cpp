#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QVariantList>
#include <QStandardPaths>
#include <QDir>

#include "OverlayItem.h"

static const char *kAppVersion   = PROJECT_VERSION;
static const char *kBuildDate    = __DATE__ " " __TIME__;
static const char *kAppUrl       = "https://github.com/landenlabs/qt-map1";
static const char *kTwcApiKey    = TWC_API_KEY;

// Load overlay layer definitions from layers.json.
// Search order:
//   1. Same directory as the executable  (deployment / build run)
//   2. LAYERS_FILE_PATH baked in at compile time (source tree during development)
static QVariantList loadLayers()
{
    QStringList candidates = {
        QDir(QCoreApplication::applicationDirPath()).filePath("layers.json"),
        QLatin1String(LAYERS_FILE_PATH),
    };

    for (const QString &path : candidates) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            continue;

        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error != QJsonParseError::NoError) {
            qWarning("layers.json parse error at %s: %s",
                     qPrintable(path), qPrintable(err.errorString()));
            continue;
        }
        if (!doc.isArray()) {
            qWarning("layers.json: expected a JSON array at %s", qPrintable(path));
            continue;
        }

        QVariantList layers;
        for (const QJsonValue &v : doc.array()) {
            if (!v.isObject()) continue;
            const QJsonObject obj = v.toObject();
            const QString name = obj.value("name").toString();
            const QString url  = obj.value("url").toString();
            if (name.isEmpty() || url.isEmpty()) continue;
            layers.append(QVariantMap{ {"name", name}, {"url", url} });
        }
        qInfo("Loaded %lld layer(s) from %s", (long long)layers.size(), qPrintable(path));
        return layers;
    }

    qWarning("layers.json not found – no dynamic overlay layers loaded.");
    return {};
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationVersion(QLatin1String(kAppVersion));

    qmlRegisterType<OverlayItem>("MapApp", 1, 0, "OverlayItem");

    QQmlApplicationEngine engine;

    engine.rootContext()->setContextProperty("appVersion",    QLatin1String(kAppVersion));
    engine.rootContext()->setContextProperty("buildDate",     QLatin1String(kBuildDate));
    engine.rootContext()->setContextProperty("appUrl",        QLatin1String(kAppUrl));
    engine.rootContext()->setContextProperty("twcApiKey",     QLatin1String(kTwcApiKey));
    engine.rootContext()->setContextProperty("overlayLayers", loadLayers());

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
