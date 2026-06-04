#include "GridLoader.h"
#include "PhaseStats.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QtEndian>
#include <QtMath>
#include <memory>

// ─── UBO layout ──────────────────────────────────────────────────────────────
// Must match shaders/floatgrid.vert and shaders/floatgrid.frag exactly.
//
//   offset  0 : mat4  qt_Matrix   64 bytes
//   offset 64 : float qt_Opacity   4 bytes
//   offset 68 : float dataMin      4 bytes
//   offset 72 : float dataMax      4 bytes
//   offset 76 : float _pad         4 bytes  (std140 tail-padding to reach 80)
//   total = 80 bytes

namespace {
constexpr int kMatrixOffset = 0;
constexpr int kOpacityOffset = 64;
constexpr int kDataMinOffset = 68;
constexpr int kDataMaxOffset = 72;
constexpr int kUBOSize = 80;
} // namespace

// ─── GridLoaderShader ─────────────────────────────────────────────────────────

GridLoaderShader::GridLoaderShader() {
    // qt_add_shaders with PREFIX "/shaders" + FILES "shaders/floatgrid.*"
    // produces paths of the form :/shaders/shaders/floatgrid.*.qsb
    setShaderFileName(VertexStage, QStringLiteral(":/shaders/shaders/floatgrid.vert.qsb"));
    setShaderFileName(FragmentStage, QStringLiteral(":/shaders/shaders/floatgrid.frag.qsb"));
}

/*
  The full pipeline for per-tile texture data:

  1. Network → QImage (GridTileCache)
  - GridTileCache::requestTileImage() is called by OverlayItem when tiles are needed.
  - GridLoader fetches the raw float grid over the network.
  - onTileReady() (GridTileCache.cpp:75) converts it to a Grayscale8 QImage via gridToImage() — this is where float values are encoded as palette UVs.
  - Emits tileImageReady(product, z, x, y, image).

  2. QImage → pending queue (OverlayItem::onTileImageReady)
  - OverlayItem::onTileImageReady() (OverlayItem.cpp:142) receives the signal and stores the QImage in m_pendingImages[key], sets m_imageDirty = true, then calls update() to schedule a repaint.

  3. QImage → QSGTexture (updatePaintNode)
  - On the render thread, updatePaintNode() (OverlayItem.cpp:245–258) drains m_pendingImages: for each entry it calls window()->createTextureFromImage() to create a QSGTexture and stores it in TileGridRootNode::textures[key].
  - The geometry nodes are then rebuilt (OverlayItem.cpp:270+), each FloatGridMaterial::texture is pointed at the relevant QSGTexture.

  4. QSGTexture → GPU (FloatGridShader::updateSampledImage)
  - The Qt scene graph renderer calls updateSampledImage(), which calls commitTextureOperations() to actually upload the pixel data to the GPU.

  GridLoader (network)
    → GridTileCache::onTileReady()       gridToImage() → QImage
      → emit tileImageReady
        → OverlayItem::onTileImageReady()  m_pendingImages[key] = image; update()
          → updatePaintNode()              createTextureFromImage() → QSGTexture in root->textures
            → FloatGridShader::updateSampledImage()  commitTextureOperations() → GPU


----
  Here is the call sequence:

  1. QML engine flags the scene graph dirty (e.g., after QQuickItem::update() or a node geometry change).
  2. The render thread runs a frame. The scene graph renderer walks every QSGGeometryNode that needs drawing.
  3. For each node, the renderer calls QSGMaterialShader::updateUniformData() to upload the UBO (matrix, opacity, etc.) to the GPU.
  4. For each sampler binding, it calls QSGMaterialShader::updateSampledImage() to bind the textures — this is where commitTextureOperations() is called to actually push the pixel data.

  So the chain is:

  QQuickItem::update()          ← you call this (main thread)
    → scene graph marks dirty
      → render thread frame
        → FloatGridShader::updateUniformData()   ← Qt calls this
        → FloatGridShader::updateSampledImage()  ← Qt calls this
*/
bool GridLoaderShader::updateUniformData(RenderState &state, QSGMaterial *newMat, QSGMaterial * /*oldMat*/) {
    auto *mat = static_cast<GridLoader *>(newMat);
    QByteArray *buf = state.uniformData();
    Q_ASSERT(buf->size() >= kUBOSize);

    bool changed = false;

    if (state.isMatrixDirty()) {
        const QMatrix4x4 m = state.combinedMatrix();
        memcpy(buf->data() + kMatrixOffset, m.constData(), 64);
        changed = true;
    }
    if (state.isOpacityDirty()) {
        const float op = state.opacity();
        memcpy(buf->data() + kOpacityOffset, &op, 4);
        changed = true;
    }

    // Always upload custom uniforms; the material compare() drives batching.
    memcpy(buf->data() + kDataMinOffset, &mat->dataMin, 4);
    memcpy(buf->data() + kDataMaxOffset, &mat->dataMax, 4);
    changed = true;

    return changed;
}

void GridLoaderShader::updateSampledImage(RenderState &state, int binding, QSGTexture **texture, QSGMaterial *newMat, QSGMaterial * /*oldMat*/) {
    if (binding == 1) {
        QSGTexture *tex = static_cast<GridLoader *>(newMat)->texture;
        if (tex) {
            // commitTextureOperations uploads pixel data to the GPU.
            tex->commitTextureOperations(state.rhi(), state.resourceUpdateBatch());
            *texture = tex;
        }
    }
}

// ─── GridLoader ───────────────────────────────────────────────────────────────
//  Worth saving to memory?
// The "QNetworkAccessManager perf on the GUI thread will saturate at ~1 request/sec"
// finding is exactly the kind of non-obvious Qt gotcha that's hard to rediscover.


GridLoader::GridLoader(const QString &apiKey, QObject *parent)
    : QObject(parent)
    , m_apiKey(apiKey) {
    setFlag(Blending, true);

    // Bypass QNAM's 6-connections-per-host cap by maintaining a pool.  Each
    // QNetworkAccessManager has its own independent connection budget.
    constexpr int kNetworkPoolSize = 4;
    m_networks.reserve(kNetworkPoolSize);
    for (int i = 0; i < kNetworkPoolSize; ++i)
        m_networks.append(new QNetworkAccessManager(this));
    qInfo("GridLoader: network pool size = %d  (×6 conn/host = %d concurrent)",
          kNetworkPoolSize, kNetworkPoolSize * 6);
}

QNetworkAccessManager *GridLoader::nextNetwork() {
    QNetworkAccessManager *n = m_networks[m_networkRR];
    m_networkRR = (m_networkRR + 1) % m_networks.size();
    return n;
}

QSGMaterialType *GridLoader::type() const {
    static QSGMaterialType sType;
    return &sType;
}

QSGMaterialShader *GridLoader::createShader(QSGRendererInterface::RenderMode) const {
    return new GridLoaderShader();
}

int GridLoader::compare(const QSGMaterial *other) const {
    const auto *o = static_cast<const GridLoader *>(other);
    const void *a = texture;
    const void *b = o->texture;
    return (a < b) ? -1 : (a > b) ? 1 : 0;
}

// ─── fetchTile ────────────────────────────────────────────────────────────────
// Entry point. Checks m_infoCache first; if the product info is already known
// the stage-1 network request is skipped entirely.

void GridLoader::fetchTile(const QString &product, const QString &type, const QString &urlInfo, const QString &urlData, int x, int y, int z) {
    const PendingTile pending{product, type, urlData, x, y, z};

    // ── Cache hit: skip stage-1 ───────────────────────────────────────────────
    auto it = m_infoCache.constFind(product);
    if (it != m_infoCache.constEnd()) {
        const qint64 t = selectT(it->tValues);
        qInfo("GridLoader: info cache hit '%s' tile %d,%d,%d  t=%lld", qPrintable(product), x, y, z, static_cast<long long>(t));
        startTileFetch(pending, it->rt, t);
        return;
    }

    // ── In-flight info GET for this product: queue and wait ───────────────────
    // Without this, every tile in the first viewport burst sees an empty
    // m_infoCache and fires its own stage-1 GET.  We want exactly one info
    // GET per product per session.
    auto qit = m_infoQueue.find(product);
    if (qit != m_infoQueue.end()) {
        qit.value().append(pending);
        PhaseStats::instance().incr("info_coalesced");
        return;
    }

    // ── First request for this product: start the queue and issue stage-1 ────
    m_infoQueue.insert(product, QList<PendingTile>{pending});

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("products"), product);
    query.addQueryItem(QStringLiteral("apiKey"), m_apiKey);
    query.addQueryItem(QStringLiteral("meta"), QStringLiteral("true"));

    QUrl url(urlInfo.section(QLatin1Char('?'), 0, 0));
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("qt-map1/1.0"));
    req.setRawHeader("Accept", "application/json");
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);

    QNetworkReply *reply = nextNetwork()->get(req);
    QElapsedTimer timer; timer.start();
    connect(reply, &QNetworkReply::finished, this, [this, reply, pending, timer]() {
        PhaseStats::instance().record("info_fetch", timer.nsecsElapsed());
        handleInfoReply(reply, pending);
        reply->deleteLater();
    });

    qInfo("GridLoader: fetching info for product '%s' tile %d,%d,%d", qPrintable(product), x, y, z);
}

// ─── handleInfoReply ──────────────────────────────────────────────────────────
// Stage-1 handler.  Mirrors the JSON navigation in Python load_product_info()
// and the upper half of fetch_tile().
//
// JSON structure navigated:
//   { "layers": {
//       "<prodCode>": {
//         "<prodName>": {
//           "dimensions": [ { "rt": ["<ms>"], "t": ["<ms>", …] } ],
//           "meta": { "description":…, "dataType":…,
//                     "attributes": { "units":…, "missing_value":… } }
//         }
//       }
//     }
//   }

void GridLoader::handleInfoReply(QNetworkReply *reply, const PendingTile &pending) {
    // Take ownership of the queue.  The original `pending` is already in it.
    // Any early-return below leaves m_infoQueue without an entry for this product,
    // which is what we want — the cache's onTileError will fan failure out to all
    // m_inFlight tiles for this product.
    const QList<PendingTile> queued = m_infoQueue.take(pending.product);

    if (reply->error() != QNetworkReply::NoError) {
        emit tileError(pending.product, QStringLiteral("Info fetch failed: ") + reply->errorString());
        return;
    }

    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &parseErr);
    if (parseErr.error != QJsonParseError::NoError) {
        emit tileError(pending.product, QStringLiteral("Info JSON parse error: ") + parseErr.errorString());
        return;
    }

    // Mirrors: prod_code, prod_name = product.split(':', 1)
    const int sep = pending.product.indexOf(QLatin1Char(':'));
    const QString prodCode = (sep >= 0) ? pending.product.left(sep) : pending.product;
    const QString prodName = (sep >= 0) ? pending.product.mid(sep + 1) : QString();

    // Mirrors: layers = data.get("layers", {})
    const QJsonObject layers = doc.object().value(QStringLiteral("layers")).toObject();
    // Mirrors: prod_layer = layers.get(prod_code)
    const QJsonObject prodLayer = layers.value(prodCode).toObject();
    if (prodLayer.isEmpty()) {
        emit tileError(pending.product, QStringLiteral("Product code '") + prodCode + QStringLiteral("' not found in layers"));
        return;
    }

    // Mirrors: prod_entry = prod_layer.get(prod_name)
    const QJsonObject prodEntry = prodLayer.value(prodName).toObject();
    if (prodEntry.isEmpty()) {
        emit tileError(
                pending.product,
                QStringLiteral("Product name '") + prodName + QStringLiteral("' not found under layer '") + prodCode + QLatin1Char('\'')
        );
        return;
    }

    // Mirrors: dimensions = prod_entry.get("dimensions", [])
    //          first_dim  = dimensions[0]
    //          rt = first_dim["rt"][0]
    //          t  = ALL values from first_dim["t"]
    const QJsonArray dimensions = prodEntry.value(QStringLiteral("dimensions")).toArray();
    if (dimensions.isEmpty()) {
        emit tileError(pending.product, QStringLiteral("No dimensions for ") + pending.product);
        return;
    }
    const QJsonObject firstDim = dimensions.first().toObject();
    const QString rt = firstDim.value(QStringLiteral("rt")).toArray().first().toString();

    // Extract ALL t timestamps and select the one closest to now in the future.
    const QJsonArray tArray = firstDim.value(QStringLiteral("t")).toArray();
    QList<qint64> tValues;
    tValues.reserve(tArray.size());
    for (const QJsonValue &v : tArray) {
        const qint64 ts = v.toString().toLongLong();
        if (ts > 0)
            tValues.append(ts);
    }
    if (tValues.isEmpty()) {
        emit tileError(pending.product, QStringLiteral("No 't' timestamps for ") + pending.product);
        return;
    }
    const qint64 selectedT = selectT(tValues);

    // Cache for subsequent tile requests with the same product.
    m_infoCache.insert(pending.product, ProductInfo{rt, tValues});

    // Mirrors: meta logging
    const QJsonObject meta = prodEntry.value(QStringLiteral("meta")).toObject();
    const QJsonObject attrs = meta.value(QStringLiteral("attributes")).toObject();
    qInfo("GridLoader: info product=%s  rt=%s  t-count=%d  selected-t=%lld  queued=%lld",
          qPrintable(pending.product),
          qPrintable(rt),
          (int) tValues.size(),
          static_cast<long long>(selectedT),
          static_cast<long long>(queued.size()));
    qInfo("  description=%s  dataType=%s  units=%s  missing=%s",
          qPrintable(meta.value(QStringLiteral("description")).toString()),
          qPrintable(meta.value(QStringLiteral("dataType")).toString()),
          qPrintable(attrs.value(QStringLiteral("units")).toString()),
          qPrintable(attrs.value(QStringLiteral("missing_value")).toString()));

    // Extract lod range from meta -> tileset -> "Web Mercator" -> tiles
    const QJsonArray wmTiles = meta.value(QStringLiteral("tileset"))
                                       .toObject()
                                       .value(QStringLiteral("Web Mercator"))
                                       .toObject()
                                       .value(QStringLiteral("tiles"))
                                       .toArray();

    if (!wmTiles.isEmpty()) {
        int lodMin = INT_MAX;
        int lodMax = INT_MIN;
        for (const QJsonValue &entry : wmTiles) {
            const int lod = entry.toObject().value(QStringLiteral("lod")).toInt();
            if (lod < lodMin)
                lodMin = lod;
            if (lod > lodMax)
                lodMax = lod;
        }
        qInfo("  Web Mercator lod range: %d – %d  (%lld tiles)", lodMin, lodMax, static_cast<long long>(wmTiles.size()));
    } else {
        qInfo("  Web Mercator tileset not found in meta");
    }

    // Drain the queue: kick off stage-2 for every tile that was waiting on this
    // product's info.  Including the original `pending` (it's already in queued).
    for (const PendingTile &p : queued)
        startTileFetch(p, rt, selectedT);
}

// ─── selectT ─────────────────────────────────────────────────────────────────
// Returns the epoch-second timestamp from tValues that is closest to now and
// still in the future.  If all values are in the past, returns the most recent
// past value instead.

qint64 GridLoader::selectT(const QList<qint64> &tValues) {
    const qint64 now = QDateTime::currentSecsSinceEpoch();

    // Nearest future value (smallest t >= now).
    qint64 best = -1;
    for (qint64 t : tValues) {
        if (t >= now && (best < 0 || t < best))
            best = t;
    }
    if (best >= 0)
        return best;

    // All in the past — pick the most recent.
    for (qint64 t : tValues) {
        if (best < 0 || t > best)
            best = t;
    }
    return best;
}

// ─── startTileFetch ───────────────────────────────────────────────────────────
// Stage-2: builds the tile data URL and fires the network request.
// Called either directly from the cache path or at the end of handleInfoReply.

void GridLoader::startTileFetch(const PendingTile &pending, const QString &rt, qint64 t) {
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("products"), pending.product);
    query.addQueryItem(QStringLiteral("rt"), rt);
    query.addQueryItem(QStringLiteral("t"), QString::number(t));
    query.addQueryItem(QStringLiteral("lod"), QString::number(pending.z));
    query.addQueryItem(QStringLiteral("x"), QString::number(pending.x));
    query.addQueryItem(QStringLiteral("y"), QString::number(pending.y));
    query.addQueryItem(QStringLiteral("apiKey"), m_apiKey);

    QUrl tileUrl(pending.urlData.section(QLatin1Char('?'), 0, 0));
    tileUrl.setQuery(query);

    QNetworkRequest tileReq(tileUrl);
    tileReq.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("qt-map1/1.0"));
    tileReq.setRawHeader("Accept", "application/octet-stream");
    // Disable gzip: Qt adds Accept-Encoding:gzip by default, which would silently
    // corrupt the raw binary float4 payload and can cause a 406 response.
    tileReq.setRawHeader("Accept-Encoding", "identity");
    // Allow HTTP/2 multiplexing where the server supports it — lifts the 6
    // connections-per-host cap that otherwise serialises tile downloads.
    tileReq.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);

    QNetworkReply *tileReply = nextNetwork()->get(tileReq);

    // Track the reply so we can abort it if the tile scrolls off-screen.
    const QString key = pending.product + QLatin1Char(':')
                      + QString::number(pending.z) + QLatin1Char(':')
                      + QString::number(pending.x) + QLatin1Char(':')
                      + QString::number(pending.y);
    m_tileReplies.insert(key, tileReply);

    // Log new concurrency peaks so we can see whether QNAM is actually using
    // its 6 connections-per-host budget.
    {
        static int sPeak = 0;
        const int inflight = m_tileReplies.size();
        if (inflight > sPeak) {
            sPeak = inflight;
            qInfo("GridLoader: new peak concurrent tile fetches = %d", inflight);
        }
    }

    // Per-reply timing state.  shared_ptr so metaDataChanged and finished
    // lambdas can both reach the same QElapsedTimer + first-byte marker.
    struct ReplyTiming {
        QElapsedTimer timer;
        qint64        firstByteNs = -1;
    };
    auto timing = std::make_shared<ReplyTiming>();
    timing->timer.start();

    connect(tileReply, &QNetworkReply::metaDataChanged, this, [timing]() {
        if (timing->firstByteNs < 0)
            timing->firstByteNs = timing->timer.nsecsElapsed();
    });

    connect(tileReply, &QNetworkReply::finished, this, [this, tileReply, pending, timing, key]() {
        const qint64 totalNs = timing->timer.nsecsElapsed();
        PhaseStats::instance().record("tile_network", totalNs);
        if (timing->firstByteNs >= 0) {
            PhaseStats::instance().record("tile_queue", timing->firstByteNs);
            PhaseStats::instance().record("tile_body",  totalNs - timing->firstByteNs);
        }

        // Which HTTP version did we actually negotiate?
        const bool h2 = tileReply->attribute(QNetworkRequest::Http2WasUsedAttribute).toBool();
        PhaseStats::instance().incr(h2 ? "proto_h2" : "proto_h1");

        m_tileReplies.remove(key);
        handleTileReply(tileReply, pending);
        tileReply->deleteLater();
    });

    qInfo("GridLoader: fetching tile %d,%d,%d for '%s'  t=%lld", pending.x, pending.y, pending.z, qPrintable(pending.product), static_cast<long long>(t)
    );
}

// ─── handleTileReply ──────────────────────────────────────────────────────────
// Stage-2 handler.  Mirrors make_binary_request() + the float4 unpack block
// at the bottom of fetch_tile().

void GridLoader::handleTileReply(QNetworkReply *reply, const PendingTile &pending) {
    // Aborted by cancelTile() — silent, no error fan-out.  The cache already
    // removed this key from m_inFlight before requesting cancellation.
    if (reply->error() == QNetworkReply::OperationCanceledError) {
        PhaseStats::instance().incr("tile_canceled");
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        emit tileError(pending.product, QStringLiteral("Tile fetch failed: ") + reply->errorString());
        return;
    }

    const QByteArray data = reply->readAll();

    // Mirrors: if tile_data is None / len check
    if (data.isEmpty()) {
        emit tileError(
                pending.product,
                QStringLiteral("No tile data for ") + pending.product + QStringLiteral(" tile=") + QString::number(pending.x) + QLatin1Char(',')
                        + QString::number(pending.y) + QLatin1Char(',') + QString::number(pending.z)
        );
        return;
    }

    qInfo("GridLoader: tile %d,%d,%d — %d bytes received", pending.x, pending.y, pending.z, (int) data.size());

    // Diagnostic load-only mode: skip parseFloat4 and emit an empty grid.
    // GridTileCache::onTileReady is responsible for cleanup in this mode.
    if (m_loadOnly) {
        PhaseStats::instance().incr("load_only_skip");
        emit tileReady(pending.product, pending.x, pending.y, pending.z, QVector<QVector<float>>());
        return;
    }

    // Mirrors: data_type, byte_order = type.split(':', 1)
    //          if data_type == 'float4': …
    const QString dataType = pending.type.section(QLatin1Char(':'), 0, 0);

    if (dataType == QLatin1String("float4")) {
        QVector<QVector<float>> grid;
        {
            PhaseStats::Scope s("parse_float4");
            grid = parseFloat4(data);
        }
        emit tileReady(pending.product, pending.x, pending.y, pending.z, grid);
    } else {
        emit tileError(pending.product, QStringLiteral("Unsupported type '") + dataType + QLatin1Char('\''));
    }
}

// ─── parseFloat4 ─────────────────────────────────────────────────────────────
// Mirrors Python:
//   num_floats = len(tile_data) // 4
//   flat = struct.unpack(f'>{num_floats}f', tile_data)   # big-endian
//   side = int(math.sqrt(num_floats))
//   grid = [list(flat[row*side:(row+1)*side]) for row in range(side)]

// ─── cancelTile ──────────────────────────────────────────────────────────────
// Abort a stage-2 tile request if one is active for `key`.  Tiles still waiting
// on stage-1 (queued in m_infoQueue) are left to complete — info is shared by
// all tiles of a product and is cheap.

void GridLoader::cancelTile(const QString &key) {
    auto it = m_tileReplies.find(key);
    if (it == m_tileReplies.end())
        return;
    QNetworkReply *r = it.value();
    m_tileReplies.erase(it);
    r->abort();  // → finished signal → handleTileReply (canceled branch) → deleteLater
}

// ─── parseFloat4 ─────────────────────────────────────────────────────────────

QVector<QVector<float>> GridLoader::parseFloat4(const QByteArray &data) {
    const int numFloats = data.size() / 4;
    const int side = static_cast<int>(qSqrt(static_cast<qreal>(numFloats)));
    const auto *raw = reinterpret_cast<const uchar *>(data.constData());

    QVector<QVector<float>> grid(side, QVector<float>(side, 0.0f));

    for (int i = 0; i < side * side; ++i) {
        // Read four bytes and swap from big-endian to host byte order.
        quint32 be;
        memcpy(&be, raw + i * 4, 4);
        be = qFromBigEndian(be);

        // Reinterpret the bit pattern as IEEE-754 float (no numeric conversion).
        float val;
        memcpy(&val, &be, 4);

        grid[i / side][i % side] = val;
    }

    return grid;
}
