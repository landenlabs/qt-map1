#include "OverlayItem.h"
#include "FloatGridMaterial.h"
#include "GridLoader.h"
#include "GridTileCache.h"

#include <QSGGeometryNode>
#include <QSGGeometry>
#include <QQuickWindow>
#include <cmath>

// Special key used for the fallback test-grid texture (z > 2 or no product).
static const QString kTestKey = QStringLiteral("__test__");

// ─── TileGridRootNode ─────────────────────────────────────────────────────────
//
// Owns all QSGTextures (one per tile key).  Child QSGGeometryNodes hold raw
// non-owning pointers so the scene graph can delete them safely on the render
// thread without touching textures.

struct TileGridRootNode : public QSGNode {
    // key → texture.  Entries accumulate; eviction is managed by GridTileCache.
    QHash<QString, QSGTexture *> textures;

    ~TileGridRootNode() {
        for (QSGTexture *t : std::as_const(textures))
            delete t;
    }
};

// ─── Construction / destruction ──────────────────────────────────────────────

OverlayItem::OverlayItem(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);

    m_tileCache = new GridTileCache(QLatin1String(SUN_API_KEY), 64 * 1024 * 1024, this);
    connect(m_tileCache, &GridTileCache::tileImageReady,
            this,        &OverlayItem::onTileImageReady);
    connect(m_tileCache, &GridTileCache::tileImageError,
            this, [](const QString &product, int z, int x, int y, const QString &msg) {
        qWarning("OverlayItem: tileImageError product=%s z=%d x=%d y=%d: %s",
                 qPrintable(product), z, x, y, qPrintable(msg));
    });

    m_gridLoader = new GridLoader(QLatin1String(SUN_API_KEY), this);
    connect(m_gridLoader, &GridLoader::tileReady,
            this, [](const QString &product, int x, int y, int z,
                     const QVector<QVector<float>> &grid) {
        qInfo("OverlayItem: tileReady product=%s tile=%d,%d,%d grid=%dx%d",
              qPrintable(product), x, y, z,
              grid.size(), grid.isEmpty() ? 0 : grid[0].size());
    });
    connect(m_gridLoader, &GridLoader::tileError,
            this, [](const QString &product, const QString &message) {
        qWarning("OverlayItem: tileError product=%s: %s",
                 qPrintable(product), qPrintable(message));
    });
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
    m_rectsDirty = true;
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

// ─── setGridProduct ───────────────────────────────────────────────────────────

void OverlayItem::setGridProduct(const QString &product, const QString &type)
{
    m_product     = product;
    m_productType = type;
}

// ─── test ────────────────────────────────────────────────────────────────────

void OverlayItem::test()
{
    qInfo("OverlayItem::test — calling GridLoader::fetchTile");
    m_gridLoader->fetchTile(
        QStringLiteral("1248:Temperaturesurface"),
        QStringLiteral("float4"),
        /*x=*/2, /*y=*/2, /*z=*/2);
}

// ─── drawTile ────────────────────────────────────────────────────────────────
// Requests the image for a single tile.  For z ≤ 2 with a known product the
// request goes through GridTileCache (async; onTileImageReady delivers result).
// For z > 2 or no product a static test grid is stored immediately.

void OverlayItem::drawTile(int z, int x, int y)
{
    if (z <= 2 && !m_product.isEmpty()) {
        m_tileCache->requestTileImage(m_product, m_productType, z, x, y);
    } else {
        m_pendingImages[kTestKey] = makeTestGrid(256, 256);
        m_imageDirty = true;
        update();
    }
}

// ─── onTileImageReady ─────────────────────────────────────────────────────────
// Called by GridTileCache (on the GUI thread) when a tile image is ready.
// Stores the image under its tile key; updatePaintNode uploads it to GPU.

void OverlayItem::onTileImageReady(const QString &product, int z, int x, int y,
                                   const QImage &image)
{
    const QString key = GridTileCache::tileKey(product, z, x, y);
    m_pendingImages[key] = image;
    m_imageDirty = true;
    update();
}

// ─── setVisibleTiles ─────────────────────────────────────────────────────────
// Called from QML after every pan/zoom.  Each element of `tiles` must be a
// QVariantMap with keys: "z" (int), "x" (int), "y" (int), "rect" (QRectF).
//
// For each tile at z ≤ 2 with an active product a GridTileCache fetch is
// triggered.  For z > 2 the fallback test texture is used.

void OverlayItem::setVisibleTiles(const QVariantList &tiles)
{
    m_tileInfos.clear();
    m_tileInfos.reserve(tiles.size());

    for (const QVariant &v : tiles) {
        const QVariantMap m = v.toMap();
        TileInfo ti;
        ti.screenRect = m.value(QStringLiteral("rect")).toRectF();
        ti.z = m.value(QStringLiteral("z")).toInt();
        ti.x = m.value(QStringLiteral("x")).toInt();
        ti.y = m.value(QStringLiteral("y")).toInt();
        m_tileInfos.append(ti);

        // Request tile data for lod levels the API supports (z ≤ 2).
        if (!m_product.isEmpty()) {
            if (ti.z <= 2) {
                m_tileCache->requestTileImage(m_product, m_productType,
                                              ti.z, ti.x, ti.y);
            } else if (!m_pendingImages.contains(kTestKey)) {
                // Ensure the test texture exists for higher zoom levels.
                m_pendingImages[kTestKey] = makeTestGrid(256, 256);
                m_imageDirty = true;
            }
        }
    }

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
    update();
}

// ─── Scene-graph rendering ───────────────────────────────────────────────────
// Called on the render thread while the GUI thread is blocked (SG sync phase).
//
// Node tree:
//   TileGridRootNode              owns QHash<key, QSGTexture*>
//     QSGGeometryNode [tile 0]    material → raw non-owning texture ptr
//     QSGGeometryNode [tile 1]    material → raw non-owning texture ptr
//     ...

QSGNode *OverlayItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    if (m_tileInfos.isEmpty()) {
        delete oldNode;
        return nullptr;
    }

    auto *root = static_cast<TileGridRootNode *>(oldNode);
    if (!root) {
        root = new TileGridRootNode;
        m_imageDirty = true;
        m_rectsDirty = true;
    }

    // ── Upload any pending images to GPU textures ────────────────────────────
    if (m_imageDirty) {
        for (auto it = m_pendingImages.cbegin(); it != m_pendingImages.cend(); ++it) {
            QSGTexture *tex = window()->createTextureFromImage(it.value());
            tex->setFiltering(QSGTexture::Linear);
            tex->setHorizontalWrapMode(QSGTexture::ClampToEdge);
            tex->setVerticalWrapMode(QSGTexture::ClampToEdge);

            // Replace any existing texture for this key.
            delete root->textures.value(it.key(), nullptr);
            root->textures[it.key()] = tex;
        }
        m_pendingImages.clear();
        m_imageDirty = false;
        m_rectsDirty = true;
    }

    // ── Rebuild geometry nodes when tile layout or textures changed ──────────
    if (m_rectsDirty) {
        // Remove all existing child nodes (materials/geometries are owned).
        QSGNode *child = root->firstChild();
        while (child) {
            QSGNode *next = child->nextSibling();
            root->removeChildNode(child);
            delete child;
            child = next;
        }

        for (const TileInfo &ti : std::as_const(m_tileInfos)) {
            // Pick the best available texture for this tile.
            const QString key = m_product.isEmpty()
                ? kTestKey
                : GridTileCache::tileKey(m_product, ti.z, ti.x, ti.y);

            QSGTexture *tex = root->textures.value(key, nullptr);
            if (!tex) {
                // Fall back to the test texture if the live tile isn't ready yet.
                tex = root->textures.value(kTestKey, nullptr);
            }
            if (!tex)
                continue;   // nothing to show for this tile yet

            const float x0 = float(ti.screenRect.left());
            const float y0 = float(ti.screenRect.top());
            const float x1 = float(ti.screenRect.right());
            const float y1 = float(ti.screenRect.bottom());

            auto *geo = new QSGGeometry(
                QSGGeometry::defaultAttributes_TexturedPoint2D(), 4);
            geo->setDrawingMode(QSGGeometry::DrawTriangleStrip);
            QSGGeometry::TexturedPoint2D *vp = geo->vertexDataAsTexturedPoint2D();
            vp[0].set(x0, y0, 0.f, 0.f);
            vp[1].set(x1, y0, 1.f, 0.f);
            vp[2].set(x0, y1, 0.f, 1.f);
            vp[3].set(x1, y1, 1.f, 1.f);

            auto *mat = new FloatGridMaterial;
            mat->texture = tex;   // non-owning
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
