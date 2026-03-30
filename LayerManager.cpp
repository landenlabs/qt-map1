#include "LayerManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrl>

// ─── Construction ─────────────────────────────────────────────────────────────

LayerManager::LayerManager(const QString &layersFilePath,
                            const QString &apiKey,
                            QObject *parent)
    : QObject(parent), m_apiKey(apiKey)
{
    // Search order: exe dir first (deployed / build run), then source-tree hint.
    const QString exeDir = QDir(QCoreApplication::applicationDirPath())
                               .filePath("layers.json");
    const QString path   = QFile::exists(exeDir) ? exeDir : layersFilePath;

    m_layers = parseFile(path);

    for (const LayerDef &ld : std::as_const(m_layers)) {
        m_layersVariant.append(QVariantMap{
            { "name",        ld.name        },
            { "hasTwoStage", ld.hasTwoStage },
        });
    }
}

// ─── QML property ─────────────────────────────────────────────────────────────

QVariantList LayerManager::layers() const { return m_layersVariant; }

// ─── File parsing ─────────────────────────────────────────────────────────────

QVector<LayerManager::LayerDef> LayerManager::parseFile(const QString &path)
{
    QVector<LayerDef> result;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning("LayerManager: cannot open '%s'", qPrintable(path));
        return result;
    }

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning("LayerManager: JSON parse error in '%s': %s",
                 qPrintable(path), qPrintable(err.errorString()));
        return result;
    }
    if (!doc.isArray()) {
        qWarning("LayerManager: '%s' must be a JSON array", qPrintable(path));
        return result;
    }

    for (const QJsonValue &v : doc.array()) {
        if (!v.isObject()) continue;
        const QJsonObject obj = v.toObject();

        LayerDef ld;
        ld.name   = obj.value("name").toString().trimmed();
        ld.urlPng = obj.value("urlpng").toString().trimmed();
        ld.urlTm  = obj.value("urltm").toString().trimmed();

        if (ld.name.isEmpty()) continue;

        if (ld.urlPng.isEmpty()) {
            // Legacy single-URL format
            ld.urlPng      = obj.value("url").toString().trimmed();
            ld.hasTwoStage = false;
        } else {
            ld.hasTwoStage = !ld.urlTm.isEmpty();
        }

        if (ld.urlPng.isEmpty()) continue;

        result.append(ld);
    }

    qInfo("LayerManager: loaded %d layer(s) from '%s'",
          (int)result.size(), qPrintable(path));
    return result;
}

// ─── Helpers ──────────────────────────────────────────────────────────────────

QString LayerManager::substituteKey(const QString &tmpl) const
{
    return QString(tmpl).replace("{k}", m_apiKey);
}

// ─── Layer enable / disable ───────────────────────────────────────────────────

void LayerManager::enableLayer(int index)
{
    if (index < 0 || index >= (int)m_layers.size()) return;
    const LayerDef &ld = m_layers[index];

    if (!ld.hasTwoStage) {
        // Legacy: emit the tile URL directly (key substituted, no time needed)
        emit layerReady(index, substituteKey(ld.urlPng));
        return;
    }

    // Stage 1: fetch the time-series endpoint to resolve the {t} timestamp
    const QString tmUrl = substituteKey(ld.urlTm);
    QNetworkRequest req{QUrl(tmUrl)};
    req.setHeader(QNetworkRequest::UserAgentHeader, "qt-map1/1.0");

    QNetworkReply *reply = m_network.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, index]() {
        handleTimestampReply(reply, index);
    });

    qInfo("LayerManager: fetching time-series for layer %d '%s'",
          index, qPrintable(ld.name));
}

void LayerManager::disableLayer(int index)
{
    Q_UNUSED(index)
    // Overlay teardown wired in a future phase when tile rendering is implemented.
}

// ─── Stage-1 network reply ────────────────────────────────────────────────────

void LayerManager::handleTimestampReply(QNetworkReply *reply, int index)
{
    reply->deleteLater();

    const QString url = reply->url().toString();

    if (reply->error() != QNetworkReply::NoError) {
        emit layerError(index,
            QStringLiteral("Time-series fetch failed: url=") + url
            + QStringLiteral(" – ") + reply->errorString());
        return;
    }

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &err);
    if (err.error != QJsonParseError::NoError) {
        emit layerError(index,
            QStringLiteral("Time-series JSON parse error: url=") + url
            + QStringLiteral(" – ") + err.errorString());
        return;
    }

    // Navigate: seriesInfo → <first product key> → series[0] → fts[0]
    const QJsonObject root       = doc.object();
    const QJsonObject seriesInfo = root.value("seriesInfo").toObject();

    if (seriesInfo.isEmpty()) {
        emit layerError(index, "Time-series response missing 'seriesInfo'");
        return;
    }

    // Take the first product key (e.g. "tempFcst") without assuming its name
    const QJsonObject product = seriesInfo.constBegin().value().toObject();
    const QJsonArray  series  = product.value("series").toArray();

    if (series.isEmpty()) {
        emit layerError(index, "Time-series 'series' array is empty");
        return;
    }

    const QJsonArray fts = series.first().toObject().value("fts").toArray();
    if (fts.isEmpty()) {
        emit layerError(index, "Time-series 'fts' array is empty");
        return;
    }

    const qint64 timestamp = fts.first().toInteger();

    // Substitute {k} and {t}; leave {x}, {y}, {z} for the tile fetcher
    const QString tileUrl = substituteKey(m_layers[index].urlPng)
                                .replace("{t}", QString::number(timestamp));

    qInfo("LayerManager: layer %d '%s' ready – ts=%lld",
          index, qPrintable(m_layers[index].name), (long long)timestamp);

    emit layerReady(index, tileUrl);
}
