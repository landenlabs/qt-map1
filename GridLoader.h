#pragma once

#include <QSGMaterial>
#include <QSGMaterialShader>
#include <QSGTexture>
#include <QObject>
#include <QHash>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>
#include <QVector>
#include <atomic>

// ─── GridLoaderShader ─────────────────────────────────────────────────────────
//
// QSGMaterialShader that:
//   • loads pre-compiled .qsb shaders from Qt resources
//   • fills the UBO at binding 0 (matrix, opacity, dataMin, dataMax)
//   • binds the float-data texture at sampler binding 1

class GridLoaderShader : public QSGMaterialShader
{
public:
    GridLoaderShader();

    bool updateUniformData(RenderState &state,
                           QSGMaterial *newMat,
                           QSGMaterial *oldMat) override;

    void updateSampledImage(RenderState &state,
                            int binding,
                            QSGTexture **texture,
                            QSGMaterial *newMat,
                            QSGMaterial *oldMat) override;
};

// ─── GridLoader ───────────────────────────────────────────────────────────────
//
// Combined material + async tile fetcher.
//
// As a QSGMaterial it owns the QSGTexture that backs the float grid data and
// exposes the scalar range [dataMin, dataMax] used by the fragment shader.
//
// As a QObject it performs an async two-stage tile fetch (translated from the
// Python fetch_tile / load_product_info helpers):
//
//   Stage 1 (fetchTile):      GET tiler/info?products=…&apiKey=…&meta=true
//                              → parse layers JSON
//                              → extract rt[0] and ALL t values from dimensions[0]
//                              → select t closest to now that is in the future
//                              → cache ProductInfo in m_infoCache (once per product)
//   Stage 2 (startTileFetch): GET tiler/data?products=…&rt=…&t=…&lod=…&x=…&y=…
//                              → read raw bytes
//                              → unpack big-endian float4 → 2-D grid
//
// The stage-1 network fetch is skipped on subsequent calls for the same product
// (m_infoCache hit).  Endpoint URLs are sourced from grids.json via fetchTile().

class GridLoader : public QObject, public QSGMaterial
{
    Q_OBJECT

public:
    // apiKey – substituted for every request; matches SUN_API_KEY in secrets.cmake.
    explicit GridLoader(const QString &apiKey, QObject *parent = nullptr);

    // ── QSGMaterial interface ──────────────────────────────────────────────────
    QSGMaterialType   *type()   const override;
    QSGMaterialShader *createShader(QSGRendererInterface::RenderMode) const override;

    // Used by the scene graph to decide whether two draw calls can be batched.
    int compare(const QSGMaterial *other) const override;

    // Non-owning pointer — texture is owned by TileGridRootNode in OverlayItem.cpp
    QSGTexture *texture = nullptr;
    float dataMin = 0.0f;
    float dataMax = 1.0f;

    // ── Fetch interface ────────────────────────────────────────────────────────
    // Entry point.  Mirrors Python fetch_tile(api_key, product, type, verbose).
    //   product – "prodCode:prodName"  e.g. "1248:Temperaturesurface"
    //   type    – data type string; currently only "float4" is supported
    //   urlInfo – info endpoint template (query string stripped internally)
    //   urlData – data endpoint template (query string stripped internally)
    //   x,y,z  – tile coordinates
    // Diagnostic mode: skip parseFloat4 when reply arrives.  Used together with
    // GridTileCache::setLoadOnly() to measure pure network throughput.
    // Safe to call from any thread (m_loadOnly is atomic).
    void setLoadOnly(bool on) { m_loadOnly = on; }

public slots:
    // These are public slots so GridTileCache can connect to them across a
    // thread boundary (this object lives on a worker thread).
    void fetchTile(const QString &product, const QString &type,
                   const QString &urlInfo, const QString &urlData,
                   int x, int y, int z);

    // Abort the in-flight stage-2 tile request for the given key, if any.
    // key = GridTileCache::tileKey(product, z, x, y) = "product:z:x:y".
    // Info-stage requests are not cancellable — they're cheap and shared.
    void cancelTile(const QString &key);

signals:
    // Emitted when the float grid is ready.
    // grid[row][col] contains the unpacked, host-endian float values.
    void tileReady(const QString &product, int x, int y, int z,
                   const QVector<QVector<float>> &grid);

    void tileError(const QString &product, const QString &message);

private:
    // Cached product metadata from a stage-1 info response.
    struct ProductInfo {
        QString       rt;       // reference-time value (used as-is in tile URL)
        QList<qint64> tValues;  // all epoch-second timestamps from dimensions[0]["t"]
    };

    // State carried across the two async hops.
    struct PendingTile {
        QString product;   // "prodCode:prodName"
        QString type;      // "float4"
        QString urlData;   // data endpoint template (from grids.json)
        int     x, y, z;
    };

    // Stage-1 reply handler – mirrors load_product_info JSON navigation.
    void handleInfoReply(QNetworkReply *reply, const PendingTile &pending);

    // Stage-2 request – issued after stage-1 (or directly from cache).
    // t is the epoch-second timestamp selected by selectT().
    void startTileFetch(const PendingTile &pending, const QString &rt, qint64 t);

    // Stage-2 reply handler – mirrors make_binary_request + float4 unpack.
    void handleTileReply(QNetworkReply *reply, const PendingTile &pending);

    // Select the t value closest to now that is in the future.
    // Falls back to the most recent past value if all t values are in the past.
    static qint64 selectT(const QList<qint64> &tValues);

    // Unpack big-endian float4 bytes into a square 2-D grid.
    // Mirrors: struct.unpack(f'>{n}f', data) reshaped to [side][side].
    static QVector<QVector<float>> parseFloat4(const QByteArray &data);

    QString                     m_apiKey;

    // Pool of QNetworkAccessManagers — each has its own 6-conn-per-host budget,
    // so N QNAMs round-robin gives ~6N concurrent in-flight requests.  Sized
    // to match the throughput of an external multi-threaded benchmark; HTTP/2
    // multiplexing is unavailable (server is HTTP/1.1).
    QVector<QNetworkAccessManager *> m_networks;
    int                              m_networkRR = 0;
    QNetworkAccessManager           *nextNetwork();

    QHash<QString, ProductInfo> m_infoCache;  // product → cached stage-1 data

    // Per-product queue of tile requests waiting on an in-flight stage-1 info
    // fetch.  When the info reply arrives, every queued tile is fired in one
    // burst — ensuring exactly one info GET per product per session.
    QHash<QString, QList<PendingTile>> m_infoQueue;

    // Tile-stage replies indexed by GridTileCache::tileKey() so they can be
    // aborted when the viewport scrolls them off-screen.
    QHash<QString, QNetworkReply *>    m_tileReplies;

    std::atomic<bool> m_loadOnly{false};  // diagnostic: skip parseFloat4 when set
};
