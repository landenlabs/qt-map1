#include "OverlayItem.h"
#include "FloatGridMaterial.h"

#include <QSGGeometryNode>
#include <QSGGeometry>
#include <QQuickWindow>
#include <cmath>
#include <algorithm>

// ─── Construction / destruction ──────────────────────────────────────────────

OverlayItem::OverlayItem(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

OverlayItem::~OverlayItem() = default;

// ─── Properties ──────────────────────────────────────────────────────────────

QObject *OverlayItem::mapItem() const  { return m_mapItem; }
void     OverlayItem::setMapItem(QObject *item)
{
    if (m_mapItem == item) return;
    if (m_mapItem) disconnect(m_mapItem, nullptr, this, nullptr);
    m_mapItem = item;
    if (m_mapItem) {
        // Actual signal signatures from QDeclarativeGeoMap (both carry the new value)
        connect(m_mapItem, SIGNAL(centerChanged(QGeoCoordinate)),
                this, SLOT(onMapViewportChanged()));
        connect(m_mapItem, SIGNAL(zoomLevelChanged(double)),
                this, SLOT(onMapViewportChanged()));
    }
    emit mapItemChanged();
}

QString OverlayItem::endpoint() const { return m_endpoint; }
void    OverlayItem::setEndpoint(const QString &url)
{
    if (m_endpoint == url) return;
    m_endpoint = url;
    emit endpointChanged();
}

float OverlayItem::dataMin() const  { return m_dataMin; }
void  OverlayItem::setDataMin(float v)
{
    if (qFuzzyCompare(m_dataMin, v)) return;
    m_dataMin = v;
    m_dirty   = true;
    emit dataMinChanged();
    update();
}

float OverlayItem::dataMax() const  { return m_dataMax; }
void  OverlayItem::setDataMax(float v)
{
    if (qFuzzyCompare(m_dataMax, v)) return;
    m_dataMax = v;
    m_dirty   = true;
    emit dataMaxChanged();
    update();
}

// ─── drawTile ────────────────────────────────────────────────────────────────
//
// Phase 1: builds a static test grid regardless of z/x/y.
// Phase 2: replace with a network fetch to m_endpoint/{z}/{x}/{y}.bin,
//   decode the float32 binary payload, and upload as a real R32F texture.

void OverlayItem::drawTile(int z, int x, int y)
{
    Q_UNUSED(z) Q_UNUSED(x) Q_UNUSED(y)

    m_pendingImage = makeTestGrid(256, 256);
    m_dirty        = true;
    update();
}

// ─── Static test grid ────────────────────────────────────────────────────────

QImage OverlayItem::makeTestGrid(int w, int h)
{
    // 2D float grid: sine/cosine wave base + central Gaussian peak.
    // All values normalised to [0, 1].
    std::vector<float> grid(w * h);

    for (int row = 0; row < h; ++row) {
        const float ny = (float(row) / float(h - 1)) * 2.0f * float(M_PI);
        for (int col = 0; col < w; ++col) {
            const float nx = (float(col) / float(w - 1)) * 2.0f * float(M_PI);

            // Tiled sine background
            float v = 0.5f * (std::sin(nx * 3.0f) * std::cos(ny * 2.0f) + 1.0f);

            // Gaussian bump centred at (0.5, 0.5)
            const float dx = float(col) / float(w) - 0.5f;
            const float dy = float(row) / float(h) - 0.5f;
            const float g  = std::exp(-(dx * dx + dy * dy) / (2.0f * 0.08f * 0.08f));

            v = std::clamp(v * 0.35f + g * 0.65f, 0.0f, 1.0f);
            grid[row * w + col] = v;
        }
    }

    // Pack into Grayscale8: 0.0 → 0, 1.0 → 255.
    // The fragment shader will receive normalised [0, 1] values via the R channel.
    QImage img(w, h, QImage::Format_Grayscale8);
    for (int row = 0; row < h; ++row) {
        uchar *line = img.scanLine(row);
        for (int col = 0; col < w; ++col)
            line[col] = static_cast<uchar>(grid[row * w + col] * 255.0f + 0.5f);
    }
    return img;
}

// ─── Viewport change ─────────────────────────────────────────────────────────

void OverlayItem::onMapViewportChanged()
{
    // Phase 2: recompute visible tile set and re-request tiles from the endpoint.
    update();
}

// ─── Scene-graph rendering ───────────────────────────────────────────────────
//
// Called on the render thread while the GUI thread is blocked.
// Rebuilds the geometry + material node whenever new float data is pending.
//
// Phase 2 optimisation: patch the texture in-place instead of recreating the
// full node tree each time.

QSGNode *OverlayItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData * /*data*/)
{
    // NOTE: do NOT delete oldNode here — the scene graph owns it and will
    // delete it after this function returns if we hand back a different node.

    if (!m_dirty && oldNode)
        return oldNode;

    if (m_pendingImage.isNull())
        return nullptr; // scene graph deletes oldNode if needed

    const float w = float(width());
    const float h = float(height());
    if (w <= 0.0f || h <= 0.0f)
        return nullptr;

    // ── Geometry ─────────────────────────────────────────────────────────────
    // Triangle strip covering the full item rect:  TL → TR → BL → BR
    auto *node = new QSGGeometryNode;

    auto *geo = new QSGGeometry(QSGGeometry::defaultAttributes_TexturedPoint2D(), 4);
    geo->setDrawingMode(QSGGeometry::DrawTriangleStrip);
    QSGGeometry::TexturedPoint2D *v = geo->vertexDataAsTexturedPoint2D();
    v[0].set(0, 0, 0.0f, 0.0f); // top-left
    v[1].set(w, 0, 1.0f, 0.0f); // top-right
    v[2].set(0, h, 0.0f, 1.0f); // bottom-left
    v[3].set(w, h, 1.0f, 1.0f); // bottom-right
    node->setGeometry(geo);
    node->setFlag(QSGNode::OwnsGeometry);

    // ── Material + texture ────────────────────────────────────────────────────
    // createTextureFromImage() is safe to call on the render thread.
    auto *mat = new FloatGridMaterial;
    mat->dataMin = m_dataMin;
    mat->dataMax = m_dataMax;

    QSGTexture *tex = window()->createTextureFromImage(m_pendingImage);
    tex->setFiltering(QSGTexture::Linear);
    tex->setHorizontalWrapMode(QSGTexture::ClampToEdge);
    tex->setVerticalWrapMode(QSGTexture::ClampToEdge);
    mat->texture.reset(tex);

    node->setMaterial(mat);
    node->setFlag(QSGNode::OwnsMaterial);

    // Discard the now-consumed source image
    m_pendingImage = QImage();
    m_dirty        = false;

    return node; // scene graph will delete oldNode (if any)
}

void OverlayItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size())
        update();
}
