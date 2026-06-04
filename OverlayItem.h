#pragma once

#include "PaletteManager.h"

#include <QHash>
#include <QImage>
#include <QNetworkAccessManager>
#include <QQuickItem>
#include <QRectF>
#include <QStringList>
#include <QVector>
#include <QVariantList>

class GridLoader;
class GridTileCache;

// OverlayItem – transparent QQuickItem that renders per-tile float-grid data
// using the floatgrid RHI shaders and a per-palette colour strip.
//
// Data flow:
//   1. setGridProduct()  — stores product, URL templates, and palette info.
//                          Looks up the named palette in PaletteManager and
//                          marks the palette texture as dirty.
//   2. setVisibleTiles() — called on every pan/zoom; fires tile fetches and
//                          calls update() so the scene graph re-renders.
//   3. updatePaintNode() — uploads pending data textures and (if dirty) the
//                          palette strip texture; rebuilds geometry nodes.

class OverlayItem : public QQuickItem
{
    Q_OBJECT

    Q_PROPERTY(QObject *mapItem  READ mapItem  WRITE setMapItem  NOTIFY mapItemChanged)
    Q_PROPERTY(QString  endpoint READ endpoint WRITE setEndpoint NOTIFY endpointChanged)

public:
    struct TileInfo {
        QRectF screenRect;
        // UV sub-region within the texture tile.  Full tile = (0,0,1,1).
        // When z > maxLod the ancestor tile is fetched and uvRect selects the
        // child's portion of that ancestor texture.
        QRectF uvRect   { 0.0, 0.0, 1.0, 1.0 };
        int    z, x, y;                // display tile (logical zoom / col / row)
        int    texZ = 0, texX = 0, texY = 0; // texture tile (= ancestor when z > maxLod)
    };

    explicit OverlayItem(QQuickItem *parent = nullptr);
    ~OverlayItem() override;

    QObject *mapItem()  const;  void setMapItem(QObject *item);
    QString  endpoint() const;  void setEndpoint(const QString &url);

    Q_INVOKABLE void drawTile(int z, int x, int y);
    Q_INVOKABLE void test();

    // Set the active product, URL templates, and palette for live tile fetching.
    Q_INVOKABLE void setGridProduct(const QString &product, const QString &type,
                                    int maxLod,
                                    const QString &urlInfo, const QString &urlData,
                                    const QString &paletteName);

    Q_INVOKABLE void setVisibleTiles(const QVariantList &tiles);

    // Enable or disable disk caching of grid float tiles.  Memory cache is unaffected.
    Q_INVOKABLE void setGridDiskCacheEnabled(bool on);

    // Drop every entry from the in-memory tile QCache so subsequent requests
    // fall through to disk (if enabled) or the network.
    Q_INVOKABLE void clearGridMemoryCache();

    // Delete every file under the on-disk grid tile cache directory.
    Q_INVOKABLE void clearGridDiskCache();

    // Current number of entries held in the in-memory grid tile cache.
    Q_INVOKABLE int  gridMemoryCacheCount() const;

    // Diagnostic: skip all post-network processing (parse, encode, save, upload)
    // so PhaseStats reflects pure tile_network throughput.
    Q_INVOKABLE void setTileLoadOnly(bool on);

    // Reload palettes from the built-in resource plus each search directory,
    // then re-apply the current palette if one is active.
    Q_INVOKABLE void reloadPalettes(const QStringList &searchPaths);

signals:
    void mapItemChanged();
    void endpointChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;
    void     geometryChange(const QRectF &newGeometry,
                            const QRectF &oldGeometry) override;

private slots:
    void onMapViewportChanged();

private:
    static QImage makeTestGrid(int w, int h);
    void onTileImageReady(const QString &product, int z, int x, int y,
                          const QImage &image);

    QObject *m_mapItem  = nullptr;
    QString  m_endpoint;

    QHash<QString, QImage> m_pendingImages;
    QVector<TileInfo>      m_tileInfos;
    bool                   m_imageDirty   = false;
    bool                   m_rectsDirty   = false;

    // Palette strip — set by setGridProduct(), uploaded to GPU in updatePaintNode().
    QImage m_paletteImage;
    bool   m_paletteDirty  = false;

    QNetworkAccessManager m_network;
    GridLoader           *m_gridLoader    = nullptr;
    GridTileCache        *m_tileCache     = nullptr;

    QString m_product;
    QString m_productType;
    QString m_urlInfo;
    QString m_urlData;
    int     m_maxLod        = 2;

    // Palette encoding coefficients (from PaletteManager::PaletteInfo).
    QString m_paletteName;
    float   m_paletteScale  = 1.0f;
    float   m_paletteOffset = 0.0f;
    int     m_paletteNumSteps = 2;

    PaletteManager m_paletteManager;
};
