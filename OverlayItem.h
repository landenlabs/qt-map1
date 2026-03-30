#pragma once

#include <QImage>
#include <QNetworkAccessManager>
#include <QQuickItem>
#include <vector>

// OverlayItem – a transparent QQuickItem that sits on top of a QtLocation Map
// and renders per-tile floating-point data grids with an OpenGL/RHI fragment
// shader (viridis colormap).
//
// Phase 1 (current): drawTile() always renders a static sine/Gaussian test
//   grid so the shader pipeline can be verified.
// Phase 2: drawTile() will fetch a float32 binary tile from `endpoint`, decode
//   it, and replace the test grid with real data.
//
// QML usage (import MapApp 1.0):
//   OverlayItem {
//       anchors.fill: map
//       mapItem: map
//       dataMin: 0.0; dataMax: 1.0
//       opacity: 0.75
//   }

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

    // Render a tile identified by Web-Mercator tile coordinates.
    //   z – zoom level
    //   x – tile column
    //   y – tile row
    // Phase 1: ignores coordinates, renders the static test grid.
    Q_INVOKABLE void drawTile(int z, int x, int y);

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
    // Builds a static 256×256 float grid with a sine-wave background and a
    // central Gaussian peak. Returns a Grayscale8 QImage (values in [0, 1]).
    static QImage makeTestGrid(int w, int h);

    QObject *m_mapItem  = nullptr;
    QString  m_endpoint;
    float    m_dataMin  = 0.0f;
    float    m_dataMax  = 1.0f;

    // Written on the GUI thread inside drawTile(); consumed on the render thread
    // inside updatePaintNode() while the GUI thread is blocked (Qt SG sync).
    QImage m_pendingImage;
    bool   m_dirty = false;

    QNetworkAccessManager m_network;
};
