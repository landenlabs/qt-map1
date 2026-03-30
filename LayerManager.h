#pragma once

#include <QObject>
#include <QVariantList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>
#include <QVector>

// LayerManager – loads overlay layer definitions from layers.json and handles
// the two-stage tile-loading protocol used by weather data providers:
//
//   Stage 1 (enableLayer):  GET urltm → parse seriesInfo → extract fts[0]
//   Stage 2 (layerReady):   tileUrlTemplate emitted with {k} and {t} resolved;
//                           {x}, {y}, {z} remain for the tile fetcher.
//
// Layers that only carry a legacy "url" field skip stage 1 and emit immediately.
//
// QML usage:
//   layerManager.layers        – model for the Repeater (name, hasTwoStage)
//   layerManager.enableLayer(i)  – call when toggle turns ON
//   layerManager.disableLayer(i) – call when toggle turns OFF
//   Connections { target: layerManager
//     function onLayerReady(i, url) { ... }
//     function onLayerError(i, msg) { ... }
//   }

class LayerManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList layers READ layers NOTIFY layersChanged)

public:
    struct LayerDef {
        QString name;
        QString urlPng;       // tile template: {k} {t} {x} {y} {z}
        QString urlTm;        // time-series endpoint template: {k}
        bool    hasTwoStage;  // true when both urlPng + urlTm are present
    };

    // layersFilePath – primary path to layers.json (usually source-tree path
    //                  baked in at compile time; falls back to exe dir).
    // apiKey – value substituted for {k} in all URL templates.
    explicit LayerManager(const QString &layersFilePath,
                          const QString &apiKey,
                          QObject *parent = nullptr);

    QVariantList layers() const;

    // Toggle a layer on.  Triggers stage-1 fetch for two-stage layers.
    Q_INVOKABLE void enableLayer(int index);

    // Toggle a layer off.  Tile teardown wired in a future phase.
    Q_INVOKABLE void disableLayer(int index);

signals:
    void layersChanged();

    // Emitted when the tile URL template is ready.
    // tileUrlTemplate has {k} and {t} already substituted; {x}, {y}, {z} remain.
    void layerReady(int index, const QString &tileUrlTemplate);

    // Emitted on any error during stage-1 fetch or parse.
    void layerError(int index, const QString &errorMessage);

private:
    static QVector<LayerDef> parseFile(const QString &path);
    QString                  substituteKey(const QString &tmpl) const;
    void                     handleTimestampReply(QNetworkReply *reply, int index);

    QVector<LayerDef>     m_layers;
    QVariantList          m_layersVariant; // cached read-only copy for QML
    QString               m_apiKey;
    QNetworkAccessManager m_network;
};
