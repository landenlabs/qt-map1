#include "LayerManager.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrl>

// ─── Construction ─────────────────────────────────────────────────────────────

LayerManager::LayerManager(const QString &apiKey, QObject *parent)
    : QObject(parent)
    , m_apiKey(apiKey) {
    QFile f(QStringLiteral(":/data/layers.json"));
    if (f.open(QIODevice::ReadOnly))
        m_layers = parseJson(f.readAll(), QStringLiteral(":/data/layers.json"));
    rebuildVariant();
}

// ─── setApiKey ────────────────────────────────────────────────────────────────

void LayerManager::setApiKey(const QString &key) {
    m_apiKey = key;
}

// ─── reload ───────────────────────────────────────────────────────────────────

void LayerManager::reload(const QStringList &searchPaths) {
    m_layers.clear();

    QFile f(QStringLiteral(":/data/layers.json"));
    if (f.open(QIODevice::ReadOnly))
        m_layers = parseJson(f.readAll(), QStringLiteral(":/data/layers.json"));

    for (const QString &dir : searchPaths) {
        const QString path = QDir(dir).filePath(QStringLiteral("layers.json"));
        QFile ef(path);
        if (!ef.open(QIODevice::ReadOnly))
            continue;
        const QVector<LayerDef> extra = parseJson(ef.readAll(), path);
        for (const LayerDef &def : extra)
            mergeLayer(def);
    }

    rebuildVariant();
    emit layersChanged();
}

// ─── mergeLayer ───────────────────────────────────────────────────────────────

void LayerManager::mergeLayer(const LayerDef &def) {
    for (LayerDef &existing : m_layers) {
        if (existing.name == def.name) {
            existing = def;
            return;
        }
    }
    m_layers.append(def);
}

// ─── rebuildVariant ───────────────────────────────────────────────────────────

void LayerManager::rebuildVariant() {
    m_layersVariant.clear();
    for (const LayerDef &ld : std::as_const(m_layers)) {
        m_layersVariant.append(QVariantMap{
                {"name", ld.name},
                {"comment", ld.comment},
                {"hasTwoStage", ld.hasTwoStage},
        });
    }
}

// ─── QML property ─────────────────────────────────────────────────────────────

QVariantList LayerManager::layers() const {
    return m_layersVariant;
}

// ─── JSON parsing ─────────────────────────────────────────────────────────────

QVector<LayerManager::LayerDef> LayerManager::parseJson(const QByteArray &data, const QString &src) {
    QVector<LayerDef> result;

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning("LayerManager: JSON parse error in '%s': %s", qPrintable(src), qPrintable(err.errorString()));
        return result;
    }
    if (!doc.isArray()) {
        qWarning("LayerManager: '%s' must be a JSON array", qPrintable(src));
        return result;
    }

    for (const QJsonValue &v : doc.array()) {
        if (!v.isObject())
            continue;
        const QJsonObject obj = v.toObject();

        LayerDef ld;
        ld.name = obj.value("name").toString().trimmed();
        ld.urlPng = obj.value("urlpng").toString().trimmed();
        ld.urlTm = obj.value("urltm").toString().trimmed();
        ld.comment = obj.value("comment").toString().trimmed();

        if (ld.name.isEmpty())
            continue;

        if (ld.urlPng.isEmpty()) {
            // Legacy single-URL format
            ld.urlPng = obj.value("url").toString().trimmed();
            ld.hasTwoStage = false;
        } else {
            ld.hasTwoStage = !ld.urlTm.isEmpty();
        }

        if (ld.urlPng.isEmpty())
            continue;

        result.append(ld);
    }

    qInfo("LayerManager: loaded %d layer(s) from '%s'", (int) result.size(), qPrintable(src));
    return result;
}

// ─── Helpers ──────────────────────────────────────────────────────────────────

QString LayerManager::substituteKey(const QString &tmpl) const {
    return QString(tmpl).replace("{k}", m_apiKey);
}

// ─── Layer enable / disable ───────────────────────────────────────────────────

void LayerManager::enableLayer(int index) {
    if (index < 0 || index >= (int) m_layers.size())
        return;
    const LayerDef &ld = m_layers[index];

    if (!ld.hasTwoStage) {
        emit layerReady(index, substituteKey(ld.urlPng));
        return;
    }

    const QString tmUrl = substituteKey(ld.urlTm);
    QNetworkRequest req{QUrl(tmUrl)};
    req.setHeader(QNetworkRequest::UserAgentHeader, "qt-map1/1.0");

    QNetworkReply *reply = m_network.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, index]() { handleTimestampReply(reply, index); });

    qInfo("LayerManager: fetching time-series for layer %d '%s'", index, qPrintable(ld.name));
}

void LayerManager::disableLayer(int index) {
    Q_UNUSED(index)
}

// ─── Stage-1 network reply ────────────────────────────────────────────────────

void LayerManager::handleTimestampReply(QNetworkReply *reply, int index) {
    reply->deleteLater();

    const QString url = reply->url().toString();

    if (reply->error() != QNetworkReply::NoError) {
        emit layerError(index, QStringLiteral("Time-series fetch failed: url=") + url + QStringLiteral(" – ") + reply->errorString());
        return;
    }

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &err);
    if (err.error != QJsonParseError::NoError) {
        emit layerError(index, QStringLiteral("Time-series JSON parse error: url=") + url + QStringLiteral(" – ") + err.errorString());
        return;
    }

    const QJsonObject root = doc.object();
    const QJsonObject seriesInfo = root.value("seriesInfo").toObject();

    if (seriesInfo.isEmpty()) {
        emit layerError(index, "Time-series response missing 'seriesInfo'");
        return;
    }

    const QJsonObject product = seriesInfo.constBegin().value().toObject();
    const QJsonArray series = product.value("series").toArray();

    if (series.isEmpty()) {
        emit layerError(index, "Time-series 'series' array is empty");
        return;
    }

    const QJsonObject seriesObj = series.first().toObject();
    const QJsonArray ftsArr = seriesObj.value("fts").toArray();
    if (ftsArr.isEmpty()) {
        emit layerError(index, "Time-series 'fts' array is empty");
        return;
    }

    const qint64 ftsValue = ftsArr.first().toInteger();
    const qint64 tsValue = seriesObj.value("ts").toInteger();

    const QString tileUrl = substituteKey(m_layers[index].urlPng).replace("{fts}", QString::number(ftsValue)).replace("{ts}", QString::number(tsValue));

    qInfo("LayerManager: layer %d '%s' ready – fts=%lld ts=%lld", index, qPrintable(m_layers[index].name), (long long) ftsValue, (long long) tsValue);

    emit layerReady(index, tileUrl);
}
