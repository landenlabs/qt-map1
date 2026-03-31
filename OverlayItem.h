#pragma once

#include <QImage>
#include <QNetworkAccessManager>
#include <QQuickItem>
#include <QRectF>
#include <QVector>
#include <QVariantList>



// OverlayItem – a transparent QQuickItem that renders per-tile floating-point
// data grids with an OpenGL/RHI fragment shader (viridis colormap).
//
// Rendering is driven by two independent inputs:
//   1. drawTile()       — supplies the float-data image (test grid or fetched tile)
//   2. setVisibleTiles()— supplies the screen-space rect for every visible tile
//
// QML calls setVisibleTiles() on every pan/zoom so the quads stay aligned with
// the underlying OSM tiles.  One QSGGeometryNode is created per visible tile;
// all nodes share a single QSGTexture held in the root TileGridRootNode.

class OverlayItem : public QQuickItem
{
    Q_OBJECT

    Q_PROPERTY(QObject *mapItem  READ mapItem  WRITE setMapItem  NOTIFY mapItemChanged)
    Q_PROPERTY(QString  endpoint READ endpoint WRITE setEndpoint NOTIFY endpointChanged)
    Q_PROPERTY(float    dataMin  READ dataMin  WRITE setDataMin  NOTIFY dataMinChanged)
    Q_PROPERTY(float    dataMax  READ dataMax  WRITE setDataMax  NOTIFY dataMaxChanged)

public:
    explicit OverlayItem(QQuickItem *parent = nullptr);
    ~OverlayItem() override;

    QObject *mapItem()  const;  void setMapItem(QObject *item);
    QString  endpoint() const;  void setEndpoint(const QString &url);
    float    dataMin()  const;  void setDataMin(float v);
    float    dataMax()  const;  void setDataMax(float v);

    // Generate the float-data image for one tile (phase 1: static test grid).
    Q_INVOKABLE void drawTile(int z, int x, int y);

    // Pass the screen-space rects for every currently-visible tile.
    // Called from QML after each pan/zoom using map.fromCoordinate() results.
    // Each QVariant must be a QRectF (i.e. Qt.rect(...) from QML).
    Q_INVOKABLE void setVisibleTiles(const QVariantList &rects);

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

    QObject *m_mapItem  = nullptr;
    QString  m_endpoint;
    float    m_dataMin  = 0.0f;
    float    m_dataMax  = 1.0f;

    // GUI-thread state handed to the render thread during SG sync
    QImage          m_pendingImage;   // new image to upload → texture
    QVector<QRectF> m_tileRects;      // current visible tile screen rects
    bool            m_imageDirty = false;  // new image pending
    bool            m_rectsDirty = false;  // tile positions changed

    QNetworkAccessManager m_network;
};
