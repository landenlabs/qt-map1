#pragma once

#include <QSGMaterial>
#include <QSGMaterialShader>
#include <QSGTexture>
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>
#include <QVector>

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
//                              → extract rt[0] and t[0] from dimensions[0]
//   Stage 2 (handleInfoReply): GET tiler/data?products=…&rt=…&t=…&lod=…&x=…&y=…
//                              → read raw bytes
//                              → unpack big-endian float4 → 2-D grid
//
// Endpoint URLs are sourced per-call from grids.json via fetchTile().

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
    void fetchTile(const QString &product, const QString &type,
                   const QString &urlInfo, const QString &urlData,
                   int x, int y, int z);

signals:
    // Emitted when the float grid is ready.
    // grid[row][col] contains the unpacked, host-endian float values.
    void tileReady(const QString &product, int x, int y, int z,
                   const QVector<QVector<float>> &grid);

    void tileError(const QString &product, const QString &message);

private:
    // State carried across the two async hops.
    struct PendingTile {
        QString product;   // "prodCode:prodName"
        QString type;      // "float4"
        QString urlData;   // data endpoint template (from grids.json)
        int     x, y, z;
    };

    // Stage-1 reply handler – mirrors load_product_info JSON navigation.
    void handleInfoReply(QNetworkReply *reply, const PendingTile &pending);

    // Stage-2 reply handler – mirrors make_binary_request + float4 unpack.
    void handleTileReply(QNetworkReply *reply, const PendingTile &pending);

    // Unpack big-endian float4 bytes into a square 2-D grid.
    // Mirrors: struct.unpack(f'>{n}f', data) reshaped to [side][side].
    static QVector<QVector<float>> parseFloat4(const QByteArray &data);

    QString               m_apiKey;
    QNetworkAccessManager m_network;
};
