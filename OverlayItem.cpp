#include "OverlayItem.h"
#include "FloatGridMaterial.h"
#include "GridLoader.h"
#include "GridTileCache.h"

#include <QQuickWindow>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <cmath>

// Special key used for the fallback test-grid texture (z > maxLod or no product).
static const QString kTestKey = QStringLiteral("__test__");

// ─── TileGridRootNode ─────────────────────────────────────────────────────────
//
// Owns all QSGTextures: per-tile data textures keyed by tile key, and the
// single shared palette strip.

struct TileGridRootNode : public QSGNode
{
    QHash<QString, QSGTexture *> textures; // per-tile data textures
    QSGTexture *paletteTexture = nullptr;  // shared palette strip

    ~TileGridRootNode() {
        for (QSGTexture *t : std::as_const(textures))
            delete t;
        delete paletteTexture;
    }
};

// ─── Construction / destruction ──────────────────────────────────────────────

OverlayItem::OverlayItem(QQuickItem *parent)
    : QQuickItem(parent) {
    setFlag(ItemHasContents, true);

    m_tileCache = new GridTileCache(QLatin1String(SUN_API_KEY), 64 * 1024 * 1024, this);
    connect(m_tileCache, &GridTileCache::tileImageReady, this, &OverlayItem::onTileImageReady);
    connect(m_tileCache, &GridTileCache::tileImageError, this, [](const QString &product, int z, int x, int y, const QString &msg) {
        qWarning("OverlayItem: tileImageError product=%s z=%d x=%d y=%d: %s", qPrintable(product), z, x, y, qPrintable(msg));
    });

    m_gridLoader = new GridLoader(QLatin1String(SUN_API_KEY), this);
    connect(m_gridLoader, &GridLoader::tileReady, this, [](const QString &product, int x, int y, int z, const QVector<QVector<float>> &grid) {
        qInfo("OverlayItem: tileReady product=%s tile=%d,%d,%d grid=%dx%d",
              qPrintable(product),
              x,
              y,
              z,
              (int) grid.size(),
              grid.isEmpty() ? 0 : (int) grid[0].size());
    });
    connect(m_gridLoader, &GridLoader::tileError, this, [](const QString &product, const QString &message) {
        qWarning("OverlayItem: tileError product=%s: %s", qPrintable(product), qPrintable(message));
    });
}

OverlayItem::~OverlayItem() = default;

// ─── Properties ──────────────────────────────────────────────────────────────

QObject *OverlayItem::mapItem() const {
    return m_mapItem;
}
void OverlayItem::setMapItem(QObject *item) {
    if (m_mapItem == item)
        return;
    if (m_mapItem)
        disconnect(m_mapItem, nullptr, this, nullptr);
    m_mapItem = item;
    if (m_mapItem) {
        connect(m_mapItem, SIGNAL(centerChanged(QGeoCoordinate)), this, SLOT(onMapViewportChanged()));
        connect(m_mapItem, SIGNAL(zoomLevelChanged(double)), this, SLOT(onMapViewportChanged()));
    }
    emit mapItemChanged();
}

QString OverlayItem::endpoint() const {
    return m_endpoint;
}
void OverlayItem::setEndpoint(const QString &url) {
    if (m_endpoint == url)
        return;
    m_endpoint = url;
    emit endpointChanged();
}

// ─── setGridProduct ───────────────────────────────────────────────────────────

void OverlayItem::setGridProduct(
        const QString &product, const QString &type, int maxLod, const QString &urlInfo, const QString &urlData, const QString &paletteName
) {
    m_product = product;
    m_productType = type;
    m_maxLod = maxLod;
    m_urlInfo = urlInfo;
    m_urlData = urlData;
    m_paletteName = paletteName;

    const PaletteManager::PaletteInfo *pal = m_paletteManager.palette(paletteName);
    if (pal) {
        m_paletteScale = pal->scale;
        m_paletteOffset = pal->offset;
        m_paletteNumSteps = pal->numSteps;
        m_paletteImage = pal->image;
        m_paletteDirty = true;
        update();
    } else {
        qWarning("OverlayItem: palette '%s' not found", qPrintable(paletteName));
    }
}

// ─── test ────────────────────────────────────────────────────────────────────

void OverlayItem::test() {
    qInfo("OverlayItem::test — calling GridLoader::fetchTile");
    m_gridLoader->fetchTile(
            m_product.isEmpty() ? QStringLiteral("1248:Temperaturesurface") : m_product,
            m_productType.isEmpty() ? QStringLiteral("float4") : m_productType,
            m_urlInfo,
            m_urlData,
            /*x=*/2,
            /*y=*/2,
            /*z=*/2
    );
}

// ─── drawTile ────────────────────────────────────────────────────────────────
/*

*/

void OverlayItem::drawTile(int z, int x, int y) {
    if (z <= m_maxLod && !m_product.isEmpty()) {
        m_tileCache->requestTileImage(m_product, m_productType, m_urlInfo, m_urlData, m_paletteScale, m_paletteOffset, m_paletteNumSteps, z, x, y);
    } else {
        m_pendingImages[kTestKey] = makeTestGrid(256, 256);
        m_imageDirty = true;
        update();
    }
}

// ─── onTileImageReady ─────────────────────────────────────────────────────────

void OverlayItem::onTileImageReady(const QString &product, int z, int x, int y, const QImage &image) {
    const QString key = GridTileCache::tileKey(product, z, x, y);
    m_pendingImages[key] = image;
    m_imageDirty = true;
    update();
}

// ─── setVisibleTiles ─────────────────────────────────────────────────────────
/*
  Question - explain flow of who/what/when is OverlayItem::drawTile(z, x, y) called

  Answer

⏺ drawTile(z, x, y) is called from exactly one place and it is a one-off bootstrap call:

  Who: main.qml — the Connections { target: gridManager } block (main.qml:1620)

  When: When gridManager emits onGridReady(index, endpoint) — i.e., after the grid product metadata has been fetched and is ready.

  What it does:
  gridManager emits gridReady(index, endpoint)
    → main.qml: onGridReady()
        overlay.setGridProduct(...)        // sets m_product, m_urlInfo, m_urlData, palette params
        overlay.drawTile(0, 0, 0)          // ← HERE: bootstrap fetch of tile z=0,x=0,y=0
        updateOverlayTiles()               // calls setVisibleTiles() for the real viewport tiles
        overlay.visible = true

  drawTile(0,0,0) is essentially a warm-up / sanity fetch — it requests the zoom-0 world tile immediately
  after the product is configured, before setVisibleTiles() has computed which tiles are actually visible.
  It's not part of the normal tile-serving loop.

  The real tile loading path is setVisibleTiles() (called by updateOverlayTiles() in QML), which
  is what runs whenever the viewport changes and requests all currently-visible tiles
  via m_tileCache->requestTileImage() in a loop.

*/
void OverlayItem::setVisibleTiles(const QVariantList &tiles) {
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

        if (!m_product.isEmpty()) {
            if (ti.z <= m_maxLod) {
                m_tileCache->requestTileImage(
                        m_product, m_productType, m_urlInfo, m_urlData, m_paletteScale, m_paletteOffset, m_paletteNumSteps, ti.z, ti.x, ti.y
                );
            } else if (!m_pendingImages.contains(kTestKey)) {
                m_pendingImages[kTestKey] = makeTestGrid(256, 256);
                m_imageDirty = true;
            }
        }
    }

    m_rectsDirty = true;
    update();
}

// ─── Static test grid ────────────────────────────────────────────────────────

QImage OverlayItem::makeTestGrid(int w, int h) {
    std::vector<float> grid(w * h);
    for (int row = 0; row < h; ++row) {
        const float ny = (float(row) / float(h - 1)) * 2.0f * float(M_PI);
        for (int col = 0; col < w; ++col) {
            const float nx = (float(col) / float(w - 1)) * 2.0f * float(M_PI);
            float v = 0.5f * (std::sin(nx * 3.0f) * std::cos(ny * 2.0f) + 1.0f);
            const float dx = float(col) / float(w) - 0.5f;
            const float dy = float(row) / float(h) - 0.5f;
            const float g = std::exp(-(dx * dx + dy * dy) / (2.0f * 0.08f * 0.08f));
            v = std::clamp(v * 0.35f + g * 0.65f, 0.0f, 1.0f);
            grid[row * w + col] = v;
        }
    }
    // Test grid values are already [0,1]; store directly as palette UV.
    QImage img(w, h, QImage::Format_Grayscale8);
    for (int row = 0; row < h; ++row) {
        uchar *line = img.scanLine(row);
        for (int col = 0; col < w; ++col)
            line[col] = static_cast<uchar>(grid[row * w + col] * 255.0f + 0.5f);
    }
    return img;
}

// ─── Viewport change ─────────────────────────────────────────────────────────

void OverlayItem::onMapViewportChanged() {
    update();
}

// ─── Scene-graph rendering ───────────────────────────────────────────────────
//
// Node tree:
//   TileGridRootNode              owns QHash<key, QSGTexture*> + paletteTexture
//     QSGGeometryNode [tile 0]    material → non-owning data + palette texture ptrs
//     QSGGeometryNode [tile 1]    ...

QSGNode *OverlayItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) {
    if (m_tileInfos.isEmpty()) {
        delete oldNode;
        return nullptr;
    }

    auto *root = static_cast<TileGridRootNode *>(oldNode);
    if (!root) {
        root = new TileGridRootNode;
        m_imageDirty = true;
        m_rectsDirty = true;
        m_paletteDirty = true;
    }

    // ── Upload palette strip texture if it changed ───────────────────────────
    if (m_paletteDirty && !m_paletteImage.isNull()) {
        delete root->paletteTexture;
        root->paletteTexture = window()->createTextureFromImage(m_paletteImage);
        root->paletteTexture->setFiltering(QSGTexture::Linear);
        root->paletteTexture->setHorizontalWrapMode(QSGTexture::ClampToEdge);
        root->paletteTexture->setVerticalWrapMode(QSGTexture::ClampToEdge);
        m_paletteDirty = false;
        m_rectsDirty = true;
    }

    // ── Upload any pending data images to GPU textures ───────────────────────
    if (m_imageDirty) {
        for (auto it = m_pendingImages.cbegin(); it != m_pendingImages.cend(); ++it) {
            QSGTexture *tex = window()->createTextureFromImage(it.value());
            tex->setFiltering(QSGTexture::Linear);
            tex->setHorizontalWrapMode(QSGTexture::ClampToEdge);
            tex->setVerticalWrapMode(QSGTexture::ClampToEdge);

            delete root->textures.value(it.key(), nullptr);
            root->textures[it.key()] = tex;
        }
        m_pendingImages.clear();
        m_imageDirty = false;
        m_rectsDirty = true;
    }

    // ── Rebuild geometry nodes when tile layout or textures changed ──────────
    if (m_rectsDirty) {
        QSGNode *child = root->firstChild();
        while (child) {
            QSGNode *next = child->nextSibling();
            root->removeChildNode(child);
            delete child;
            child = next;
        }

        for (const TileInfo &ti : std::as_const(m_tileInfos)) {
            const QString key = m_product.isEmpty() ? kTestKey : GridTileCache::tileKey(m_product, ti.z, ti.x, ti.y);

            QSGTexture *tex = root->textures.value(key, nullptr);
            if (!tex)
                tex = root->textures.value(kTestKey, nullptr);
            if (!tex)
                continue;

            const float x0 = float(ti.screenRect.left());
            const float y0 = float(ti.screenRect.top());
            const float x1 = float(ti.screenRect.right());
            const float y1 = float(ti.screenRect.bottom());

            auto *geo = new QSGGeometry(QSGGeometry::defaultAttributes_TexturedPoint2D(), 4);
            geo->setDrawingMode(QSGGeometry::DrawTriangleStrip);
            QSGGeometry::TexturedPoint2D *vp = geo->vertexDataAsTexturedPoint2D();
            vp[0].set(x0, y0, 0.f, 0.f);
            vp[1].set(x1, y0, 1.f, 0.f);
            vp[2].set(x0, y1, 0.f, 1.f);
            vp[3].set(x1, y1, 1.f, 1.f);

            auto *mat = new FloatGridMaterial;
            mat->texture = tex;                         // non-owning
            mat->paletteTexture = root->paletteTexture; // non-owning

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

// ─── reloadPalettes ───────────────────────────────────────────────────────────

void OverlayItem::reloadPalettes(const QStringList &searchPaths) {
    m_paletteManager.reload(searchPaths);

    // Re-apply the current palette so the overlay updates without requiring
    // the user to re-toggle the grid button.
    if (!m_paletteName.isEmpty()) {
        const PaletteManager::PaletteInfo *pal = m_paletteManager.palette(m_paletteName);
        if (pal) {
            m_paletteScale = pal->scale;
            m_paletteOffset = pal->offset;
            m_paletteNumSteps = pal->numSteps;
            m_paletteImage = pal->image;
            m_paletteDirty = true;
            update();
        }
    }
}

void OverlayItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) {
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size())
        update();
}
