#pragma once

#include <QHash>
#include <QImage>
#include <QNetworkAccessManager>
#include <QQuickItem>
#include <QRectF>
#include <QVector>
#include <QVariantList>

class GridLoader;
class GridTileCache;

// OverlayItem – a transparent QQuickItem that renders per-tile floating-point
// data grids with an OpenGL/RHI fragment shader (viridis colormap).
//
// Rendering is driven by two independent inputs:
//   1. setVisibleTiles() — called on every pan/zoom; carries screen-space rects
//      AND tile coordinates (z,x,y).  For z ≤ 2 with an active product it also
//      triggers a GridTileCache fetch so each tile gets its own texture.
//   2. drawTile()        — requests a single tile image (initial load or test).
//
// Per-tile textures: TileGridRootNode holds a QHash<key, QSGTexture> so each
// visible tile quad can display independent data.  Tiles whose texture has not
// yet arrived are skipped; they appear as soon as onTileImageReady fires.

class OverlayItem : public QQuickItem
{
    Q_OBJECT

    Q_PROPERTY(QObject *mapItem  READ mapItem  WRITE setMapItem  NOTIFY mapItemChanged)
    Q_PROPERTY(QString  endpoint READ endpoint WRITE setEndpoint NOTIFY endpointChanged)
    Q_PROPERTY(float    dataMin  READ dataMin  WRITE setDataMin  NOTIFY dataMinChanged)
    Q_PROPERTY(float    dataMax  READ dataMax  WRITE setDataMax  NOTIFY dataMaxChanged)

public:
    // Tile descriptor: screen-space rect + tile grid coordinates.
    struct TileInfo {
        QRectF screenRect;
        int    z, x, y;
    };

    explicit OverlayItem(QQuickItem *parent = nullptr);
    ~OverlayItem() override;

    QObject *mapItem()  const;  void setMapItem(QObject *item);
    QString  endpoint() const;  void setEndpoint(const QString &url);
    float    dataMin()  const;  void setDataMin(float v);
    float    dataMax()  const;  void setDataMax(float v);

    // Request a tile image.
    // z ≤ 2 with an active product → fetched via GridTileCache.
    // Otherwise → static test grid.
    Q_INVOKABLE void drawTile(int z, int x, int y);

    // Fire a test fetchTile() request via the embedded GridLoader.
    Q_INVOKABLE void test();

    // Set the active product, type, URL templates, and maximum supported lod for
    // live tile fetching.  Call from QML whenever the active grid changes.
    Q_INVOKABLE void setGridProduct(const QString &product, const QString &type,
                                    int maxLod,
                                    const QString &urlInfo, const QString &urlData);

    // Pass screen-space info for every currently-visible tile.
    // Each QVariant must be a QVariantMap with keys:
    //   "z" (int), "x" (int), "y" (int), "rect" (QRectF / Qt.rect)
    // Called from QML after each pan/zoom; also triggers tile fetches.
    Q_INVOKABLE void setVisibleTiles(const QVariantList &tiles);

signals:
    void mapItemChanged();
    void endpointChanged();
    void dataMinChanged();
    void dataMaxChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;
    void     geometryChange(const QRectF &newGeometry,
                            const QRectF &oldGeometry) override;

private slots:
    void onMapViewportChanged();

private:
    static QImage makeTestGrid(int w, int h);

    // Called by GridTileCache when a tile image is ready.
    void onTileImageReady(const QString &product, int z, int x, int y,
                          const QImage &image);

    QObject *m_mapItem  = nullptr;
    QString  m_endpoint;
    float    m_dataMin  = 0.0f;
    float    m_dataMax  = 1.0f;

    // GUI-thread state handed to the render thread during SG sync.
    // m_pendingImages: new images to upload to GPU textures, keyed by tile key.
    QHash<QString, QImage> m_pendingImages;
    QVector<TileInfo>      m_tileInfos;      // current visible tiles
    bool                   m_imageDirty = false;
    bool                   m_rectsDirty = false;

    QNetworkAccessManager m_network;
    GridLoader           *m_gridLoader  = nullptr;
    GridTileCache        *m_tileCache   = nullptr;
    QString               m_product;
    QString               m_productType;
    QString               m_urlInfo;
    QString               m_urlData;
    int                   m_maxLod = 2;
};
