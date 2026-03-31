#include "GridLoader.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QtEndian>
#include <QtMath>

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
constexpr int kMatrixOffset  =  0;
constexpr int kOpacityOffset = 64;
constexpr int kDataMinOffset = 68;
constexpr int kDataMaxOffset = 72;
constexpr int kUBOSize       = 80;
} // namespace

// ─── GridLoaderShader ─────────────────────────────────────────────────────────

GridLoaderShader::GridLoaderShader()
{
    // qt_add_shaders with PREFIX "/shaders" + FILES "shaders/floatgrid.*"
    // produces paths of the form :/shaders/shaders/floatgrid.*.qsb
    setShaderFileName(VertexStage,
                      QStringLiteral(":/shaders/shaders/floatgrid.vert.qsb"));
    setShaderFileName(FragmentStage,
                      QStringLiteral(":/shaders/shaders/floatgrid.frag.qsb"));
}

bool GridLoaderShader::updateUniformData(RenderState &state,
                                         QSGMaterial *newMat,
                                         QSGMaterial * /*oldMat*/)
{
    auto       *mat = static_cast<GridLoader *>(newMat);
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

void GridLoaderShader::updateSampledImage(RenderState &state,
                                           int binding,
                                           QSGTexture **texture,
                                           QSGMaterial *newMat,
                                           QSGMaterial * /*oldMat*/)
{
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

GridLoader::GridLoader(const QString &apiKey, QObject *parent)
    : QObject(parent), m_apiKey(apiKey)
{
    setFlag(Blending, true);
}

QSGMaterialType *GridLoader::type() const
{
    static QSGMaterialType sType;
    return &sType;
}

QSGMaterialShader *GridLoader::createShader(QSGRendererInterface::RenderMode) const
{
    return new GridLoaderShader();
}

int GridLoader::compare(const QSGMaterial *other) const
{
    const auto *o = static_cast<const GridLoader *>(other);
    const void *a = texture;
    const void *b = o->texture;
    return (a < b) ? -1 : (a > b) ? 1 : 0;
}

// ─── fetchTile ────────────────────────────────────────────────────────────────
// Mirrors Python fetch_tile(): kicks off stage-1 (info) fetch.

void GridLoader::fetchTile(const QString &product,
                            const QString &type,
                            const QString &urlInfo,
                            const QString &urlData,
                            int x, int y, int z)
{
    // Stage 1: load_product_info – GET tiler/info with meta=true.
    // Strip any query string from the template; we build the query ourselves.
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("products"), product);
    query.addQueryItem(QStringLiteral("apiKey"),   m_apiKey);
    query.addQueryItem(QStringLiteral("meta"),     QStringLiteral("true"));

    QUrl url(urlInfo.section(QLatin1Char('?'), 0, 0));
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("qt-map1/1.0"));
    req.setRawHeader("Accept", "application/json");

    QNetworkReply *reply = m_network.get(req);

    const PendingTile pending{ product, type, urlData, x, y, z };
    connect(reply, &QNetworkReply::finished, this, [this, reply, pending]() {
        handleInfoReply(reply, pending);
        reply->deleteLater();
    });

    qInfo("GridLoader: fetching info for product '%s' tile %d,%d,%d",
          qPrintable(product), x, y, z);
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

void GridLoader::handleInfoReply(QNetworkReply *reply,
                                  const PendingTile &pending)
{
    if (reply->error() != QNetworkReply::NoError) {
        emit tileError(pending.product,
            QStringLiteral("Info fetch failed: ") + reply->errorString());
        return;
    }

    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &parseErr);
    if (parseErr.error != QJsonParseError::NoError) {
        emit tileError(pending.product,
            QStringLiteral("Info JSON parse error: ") + parseErr.errorString());
        return;
    }

    // Mirrors: prod_code, prod_name = product.split(':', 1)
    const int    sep      = pending.product.indexOf(QLatin1Char(':'));
    const QString prodCode = (sep >= 0) ? pending.product.left(sep)  : pending.product;
    const QString prodName = (sep >= 0) ? pending.product.mid(sep + 1) : QString();

    // Mirrors: layers = data.get("layers", {})
    const QJsonObject layers   = doc.object().value(QStringLiteral("layers")).toObject();
    // Mirrors: prod_layer = layers.get(prod_code)
    const QJsonObject prodLayer = layers.value(prodCode).toObject();
    if (prodLayer.isEmpty()) {
        emit tileError(pending.product,
            QStringLiteral("Product code '") + prodCode + QStringLiteral("' not found in layers"));
        return;
    }

    // Mirrors: prod_entry = prod_layer.get(prod_name)
    const QJsonObject prodEntry = prodLayer.value(prodName).toObject();
    if (prodEntry.isEmpty()) {
        emit tileError(pending.product,
            QStringLiteral("Product name '") + prodName
            + QStringLiteral("' not found under layer '") + prodCode + QLatin1Char('\''));
        return;
    }

    // Mirrors: dimensions = prod_entry.get("dimensions", [])
    //          first_dim  = dimensions[0]
    //          rt = first_dim["rt"][0]
    //          t  = first_dim["t"][0]
    const QJsonArray  dimensions = prodEntry.value(QStringLiteral("dimensions")).toArray();
    if (dimensions.isEmpty()) {
        emit tileError(pending.product,
            QStringLiteral("No dimensions for ") + pending.product);
        return;
    }
    const QJsonObject firstDim = dimensions.first().toObject();
    const QString rt = firstDim.value(QStringLiteral("rt")).toArray()
                                .first().toString();
    const QString t  = firstDim.value(QStringLiteral("t")).toArray()
                                .first().toString();

    // Mirrors: meta logging
    const QJsonObject meta  = prodEntry.value(QStringLiteral("meta")).toObject();
    const QJsonObject attrs = meta.value(QStringLiteral("attributes")).toObject();
    qInfo("GridLoader: Info product=%s  rt=%s  t=%s",
          qPrintable(pending.product), qPrintable(rt), qPrintable(t));
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
            if (lod < lodMin) lodMin = lod;
            if (lod > lodMax) lodMax = lod;
        }
        qInfo("  Web Mercator lod range: %d – %d  (%lld tiles)",
              lodMin, lodMax, static_cast<long long>(wmTiles.size()));
    } else {
        qInfo("  Web Mercator tileset not found in meta");
    }

    // Stage 2: make_binary_request – build tile URL and fetch raw bytes.
    // Mirrors: tile_url = SUN_DATA_URL + f"?products={product}&rt={rt}&t={t}
    //                       &lod={z}&x={x}&y={y}&apiKey={api_key}"
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("products"), pending.product);
    query.addQueryItem(QStringLiteral("rt"),       rt);
    query.addQueryItem(QStringLiteral("t"),        t);
    query.addQueryItem(QStringLiteral("lod"),      QString::number(pending.z));
    query.addQueryItem(QStringLiteral("x"),        QString::number(pending.x));
    query.addQueryItem(QStringLiteral("y"),        QString::number(pending.y));
    query.addQueryItem(QStringLiteral("apiKey"),   m_apiKey);

    QUrl tileUrl(pending.urlData.section(QLatin1Char('?'), 0, 0));
    tileUrl.setQuery(query);

    QNetworkRequest tileReq(tileUrl);
    tileReq.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("qt-map1/1.0"));
    tileReq.setRawHeader("Accept", "application/octet-stream");
    // Request uncompressed data: Qt sends Accept-Encoding:gzip by default,
    // which would silently corrupt the raw binary float4 payload if the server
    // honours it, and can also cause the server to return 406.
    tileReq.setRawHeader("Accept-Encoding", "identity");

    QNetworkReply *tileReply = m_network.get(tileReq);
    connect(tileReply, &QNetworkReply::finished, this, [this, tileReply, pending]() {
        handleTileReply(tileReply, pending);
        tileReply->deleteLater();
    });

    qInfo("GridLoader: fetching tile %d,%d,%d for '%s'",
          pending.x, pending.y, pending.z, qPrintable(pending.product));
}

// ─── handleTileReply ──────────────────────────────────────────────────────────
// Stage-2 handler.  Mirrors make_binary_request() + the float4 unpack block
// at the bottom of fetch_tile().

void GridLoader::handleTileReply(QNetworkReply *reply,
                                  const PendingTile &pending)
{
    if (reply->error() != QNetworkReply::NoError) {
        emit tileError(pending.product,
            QStringLiteral("Tile fetch failed: ") + reply->errorString());
        return;
    }

    const QByteArray data = reply->readAll();

    // Mirrors: if tile_data is None / len check
    if (data.isEmpty()) {
        emit tileError(pending.product,
            QStringLiteral("No tile data for ") + pending.product
            + QStringLiteral(" tile=")
            + QString::number(pending.x) + QLatin1Char(',')
            + QString::number(pending.y) + QLatin1Char(',')
            + QString::number(pending.z));
        return;
    }

    qInfo("GridLoader: tile %d,%d,%d — %d bytes received",
          pending.x, pending.y, pending.z, (int)data.size());

    // Mirrors: data_type, byte_order = type.split(':', 1)
    //          if data_type == 'float4': …
    const QString dataType = pending.type.section(QLatin1Char(':'), 0, 0);

    if (dataType == QLatin1String("float4")) {
        const QVector<QVector<float>> grid = parseFloat4(data);
        emit tileReady(pending.product, pending.x, pending.y, pending.z, grid);
    } else {
        emit tileError(pending.product,
            QStringLiteral("Unsupported type '") + dataType + QLatin1Char('\''));
    }
}

// ─── parseFloat4 ─────────────────────────────────────────────────────────────
// Mirrors Python:
//   num_floats = len(tile_data) // 4
//   flat = struct.unpack(f'>{num_floats}f', tile_data)   # big-endian
//   side = int(math.sqrt(num_floats))
//   grid = [list(flat[row*side:(row+1)*side]) for row in range(side)]

QVector<QVector<float>> GridLoader::parseFloat4(const QByteArray &data)
{
    const int  numFloats = data.size() / 4;
    const int  side      = static_cast<int>(qSqrt(static_cast<qreal>(numFloats)));
    const auto *raw      = reinterpret_cast<const uchar *>(data.constData());

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
