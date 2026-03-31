#pragma once

#include <QObject>
#include <QVariantList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>
#include <QStringList>
#include <QVector>

// LayerManager – loads overlay layer definitions from :/data/layers.json and
// handles the two-stage tile-loading protocol used by weather data providers.
//
// Data loading:
//   1. Built-in defaults are compiled into the app as :/data/layers.json.
//   2. reload(searchPaths) re-reads the defaults then merges any layers.json
//      found in each search directory.  External entries with the same "name"
//      replace the built-in entry; new names are appended.
//   3. Emits layersChanged() so QML Repeaters rebuild automatically.
//
// QML usage:
//   layerManager.layers          – model for the Repeater (name, hasTwoStage)
//   layerManager.enableLayer(i)  – call when toggle turns ON
//   layerManager.disableLayer(i) – call when toggle turns OFF

class LayerManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList layers READ layers NOTIFY layersChanged)

public:
    struct LayerDef {
        QString name;
        QString urlPng;
        QString urlTm;
        bool    hasTwoStage;
    };

    explicit LayerManager(const QString &apiKey, QObject *parent = nullptr);

    QVariantList layers() const;

    // Full reload: reset to resource defaults then merge any layers.json in
    // each search directory.  Duplicate names in external files replace the
    // built-in entry.  Emits layersChanged().
    void reload(const QStringList &searchPaths);

    Q_INVOKABLE void enableLayer(int index);
    Q_INVOKABLE void disableLayer(int index);

signals:
    void layersChanged();
    void layerReady(int index, const QString &tileUrlTemplate);
    void layerError(int index, const QString &errorMessage);

private:
    static QVector<LayerDef> parseJson(const QByteArray &data, const QString &src);
    void mergeLayer(const LayerDef &def);
    void rebuildVariant();
    QString substituteKey(const QString &tmpl) const;
    void handleTimestampReply(QNetworkReply *reply, int index);

    QVector<LayerDef>     m_layers;
    QVariantList          m_layersVariant;
    QString               m_apiKey;
    QNetworkAccessManager m_network;
};
