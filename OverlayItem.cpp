#include "OverlayItem.h"
#include "FloatGridMaterial.h"


#include <QSGGeometryNode>
#include <QSGGeometry>
#include <QQuickWindow>
#include <cmath>

// ─── TileGridRootNode ─────────────────────────────────────────────────────────
//
// Container node that owns the shared QSGTexture.  All child QSGGeometryNodes
// reference the texture via a raw non-owning pointer.  The scene graph deletes
// this node on the render thread, ensuring the texture is also destroyed there.

struct TileGridRootNode : public QSGNode {
    std::unique_ptr<QSGTexture> texture;
};

// ─── Construction / destruction ──────────────────────────────────────────────

OverlayItem::OverlayItem(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

OverlayItem::~OverlayItem() = default;

// ─── Properties ──────────────────────────────────────────────────────────────

QObject *OverlayItem::mapItem() const { return m_mapItem; }
void OverlayItem::setMapItem(QObject *item)
{
    if (m_mapItem == item) return;
    if (m_mapItem) disconnect(m_mapItem, nullptr, this, nullptr);
    m_mapItem = item;
    if (m_mapItem) {
        connect(m_mapItem, SIGNAL(centerChanged(QGeoCoordinate)),
                this, SLOT(onMapViewportChanged()));
        connect(m_mapItem, SIGNAL(zoomLevelChanged(double)),
                this, SLOT(onMapViewportChanged()));
    }
    emit mapItemChanged();
}

QString OverlayItem::endpoint() const { return m_endpoint; }
void OverlayItem::setEndpoint(const QString &url)
{
    if (m_endpoint == url) return;
    m_endpoint = url;
    emit endpointChanged();
}

float OverlayItem::dataMin() const { return m_dataMin; }
void OverlayItem::setDataMin(float v)
{
    if (qFuzzyCompare(m_dataMin, v)) return;
    m_dataMin = v;
    m_rectsDirty = true;  // materials need rebuilding with new range
    emit dataMinChanged();
    update();
}

float OverlayItem::dataMax() const { return m_dataMax; }
void OverlayItem::setDataMax(float v)
{
    if (qFuzzyCompare(m_dataMax, v)) return;
    m_dataMax = v;
    m_rectsDirty = true;
    emit dataMaxChanged();
    update();
}

// ─── drawTile ────────────────────────────────────────────────────────────────
//
// Phase 1: generates the static test grid image (coordinates ignored).
// Phase 2: fetch float32 binary from endpoint/{z}/{x}/{y}.bin.
//
// Only sets m_imageDirty — positions are supplied separately via setVisibleTiles().

void OverlayItem::drawTile(int z, int x, int y)
{
    Q_UNUSED(z) Q_UNUSED(x) Q_UNUSED(y)
    m_pendingImage = makeTestGrid(256, 256);
    m_imageDirty   = true;
    update();
}

// ─── setVisibleTiles ─────────────────────────────────────────────────────────
//
// Called from QML after every pan/zoom with the screen-space QRectF for each
// visible tile.  QML computes these using map.fromCoordinate() so they match
// the OSM tile grid exactly.

void OverlayItem::setVisibleTiles(const QVariantList &rects)
{
    m_tileRects.clear();
    m_tileRects.reserve(rects.size());
    for (const QVariant &v : rects)
        m_tileRects.append(v.toRectF());
    m_rectsDirty = true;
    update();
}

// ─── Static test grid ────────────────────────────────────────────────────────

QImage OverlayItem::makeTestGrid(int w, int h)
{
    std::vector<float> grid(w * h);
    for (int row = 0; row < h; ++row) {
        const float ny = (float(row) / float(h - 1)) * 2.0f * float(M_PI);
        for (int col = 0; col < w; ++col) {
            const float nx = (float(col) / float(w - 1)) * 2.0f * float(M_PI);
            float v = 0.5f * (std::sin(nx * 3.0f) * std::cos(ny * 2.0f) + 1.0f);
            const float dx = float(col) / float(w) - 0.5f;
            const float dy = float(row) / float(h) - 0.5f;
            const float g  = std::exp(-(dx * dx + dy * dy) / (2.0f * 0.08f * 0.08f));
            v = std::clamp(v * 0.35f + g * 0.65f, 0.0f, 1.0f);
            grid[row * w + col] = v;
        }
    }
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
    // QML Connections handle the tile rect update; just schedule a repaint
    // here so the node is rebuilt after QML calls setVisibleTiles().
    update();
}

// ─── Scene-graph rendering ───────────────────────────────────────────────────
//
// Called on the render thread while the GUI thread is blocked.
//
// Node tree:
//   TileGridRootNode          owns QSGTexture
//     QSGGeometryNode [tile 0]  material → raw texture ptr
//     QSGGeometryNode [tile 1]  material → raw texture ptr
//     ...

QSGNode *OverlayItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    if (m_tileRects.isEmpty()) {
        return nullptr;  // scene graph deletes oldNode
    }

    auto *root = static_cast<TileGridRootNode *>(oldNode);
    if (!root) {
        root = new TileGridRootNode;
        m_imageDirty = true;   // force texture creation on first use
        m_rectsDirty = true;
    }

    // ── Upload new texture when image data has changed ───────────────────────
    if (m_imageDirty && !m_pendingImage.isNull()) {
        QSGTexture *tex = window()->createTextureFromImage(m_pendingImage);
        tex->setFiltering(QSGTexture::Linear);
        tex->setHorizontalWrapMode(QSGTexture::ClampToEdge);
        tex->setVerticalWrapMode(QSGTexture::ClampToEdge);
        root->texture.reset(tex);
        m_pendingImage = QImage();
        m_imageDirty   = false;
        m_rectsDirty   = true;  // existing tile nodes need the new texture ptr
    }

    if (!root->texture)
        return root;   // no texture yet — return empty root

    // ── Rebuild child geometry nodes when tile rects or texture changed ──────
    if (m_rectsDirty) {
        // Delete all existing tile nodes
        QSGNode *child = root->firstChild();
        while (child) {
            QSGNode *next = child->nextSibling();
            root->removeChildNode(child);
            delete child;
            child = next;
        }

        // Create one textured quad per visible tile
        QSGTexture *sharedTex = root->texture.get();
        for (const QRectF &rect : std::as_const(m_tileRects)) {
            const float x0 = float(rect.left());
            const float y0 = float(rect.top());
            const float x1 = float(rect.right());
            const float y1 = float(rect.bottom());

            auto *geo = new QSGGeometry(
                QSGGeometry::defaultAttributes_TexturedPoint2D(), 4);
            geo->setDrawingMode(QSGGeometry::DrawTriangleStrip);
            QSGGeometry::TexturedPoint2D *v = geo->vertexDataAsTexturedPoint2D();
            v[0].set(x0, y0, 0.f, 0.f);  // top-left
            v[1].set(x1, y0, 1.f, 0.f);  // top-right
            v[2].set(x0, y1, 0.f, 1.f);  // bottom-left
            v[3].set(x1, y1, 1.f, 1.f);  // bottom-right

            auto *mat = new FloatGridMaterial;
            mat->texture = sharedTex;   // non-owning
            mat->dataMin = m_dataMin;
            mat->dataMax = m_dataMax;

            auto *node = new QSGGeometryNode;
            node->setGeometry(geo);
            node->setFlag(QSGNode::OwnsGeometry);
            node->setMaterial(mat);
            node->setFlag(QSGNode::OwnsMaterial);

            root->appendChildNode(node);
        }

        m_rectsDirty = false;
    }

    return root;
}

void OverlayItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size())
        update();
}
