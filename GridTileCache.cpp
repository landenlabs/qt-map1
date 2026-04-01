#include "GridTileCache.h"
#include "GridLoader.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>

// ─── Construction ─────────────────────────────────────────────────────────────

GridTileCache::GridTileCache(const QString &apiKey, qsizetype maxMemBytes, QObject *parent)
    : QObject(parent)
    , m_loader(new GridLoader(apiKey, this))
    , m_memCache(maxMemBytes) {
    // "p1" suffix = palette-indexed format v1; prevents stale per-tile-min/max
    // normalised tiles from being read by the new palette-UV encoding.
    m_diskCacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/grid_tiles_p1");
    QDir().mkpath(m_diskCacheDir);
    qInfo("GridTileCache: disk cache → %s", qPrintable(m_diskCacheDir));

    connect(m_loader, &GridLoader::tileReady, this, &GridTileCache::onTileReady);
    connect(m_loader, &GridLoader::tileError, this, &GridTileCache::onTileError);
}

// ─── requestTileImage ─────────────────────────────────────────────────────────

void GridTileCache::requestTileImage(
        const QString &product,
        const QString &type,
        const QString &urlInfo,
        const QString &urlData,
        float paletteScale,
        float paletteOffset,
        int numSteps,
        int z,
        int x,
        int y
) {
    const QString key = tileKey(product, z, x, y);

    // Always refresh the palette params for this product (may change if user
    // switches palette mid-session).
    m_productPalette[product] = PaletteParams{paletteScale, paletteOffset, numSteps};

    // 1. Memory cache hit
    if (QImage *cached = m_memCache[key]) {
        // qInfo("GridTileCache: mem-cache hit  %s", qPrintable(key));
        emit tileImageReady(product, z, x, y, *cached);
        return;
    }

    // 2. Disk cache hit
    QImage diskImg;
    if (loadFromDisk(key, diskImg)) {
        // qInfo("GridTileCache: disk-cache hit %s", qPrintable(key));
        m_memCache.insert(key, new QImage(diskImg), static_cast<qsizetype>(diskImg.sizeInBytes()));
        emit tileImageReady(product, z, x, y, diskImg);
        return;
    }

    // 3. Network fetch — coalesce duplicates
    if (m_inFlight.contains(key)) {
        qInfo("GridTileCache: already in-flight %s", qPrintable(key));
        return;
    }

    qInfo("GridTileCache: fetching %s", qPrintable(key));
    m_inFlight.insert(key);
    m_loader->fetchTile(product, type, urlInfo, urlData, x, y, z);
}

// ─── onTileReady ─────────────────────────────────────────────────────────────

void GridTileCache::onTileReady(const QString &product, int x, int y, int z, const QVector<QVector<float>> &grid) {
    const QString key = tileKey(product, z, x, y);
    m_inFlight.remove(key);

    // Retrieve palette params stored when the request was originally made.
    const PaletteParams pp = m_productPalette.value(product, PaletteParams{1.0f, 0.0f, 2});

    const QImage img = gridToImage(grid, pp.paletteScale, pp.paletteOffset, pp.numSteps);
    if (img.isNull()) {
        emit tileImageError(product, z, x, y, QStringLiteral("gridToImage produced null image"));
        return;
    }

    saveToDisk(key, img);
    m_memCache.insert(key, new QImage(img), static_cast<qsizetype>(img.sizeInBytes()));

    qInfo("GridTileCache: cached %s  (%dx%d)", qPrintable(key), img.width(), img.height());
    emit tileImageReady(product, z, x, y, img);
}

// ─── onTileError ─────────────────────────────────────────────────────────────

void GridTileCache::onTileError(const QString &product, const QString &message) {
    const QString prefix = product + QLatin1Char(':');
    QList<QString> toRemove;
    for (const QString &key : m_inFlight)
        if (key.startsWith(prefix))
            toRemove.append(key);

    for (const QString &key : toRemove) {
        m_inFlight.remove(key);
        const QStringList parts = key.mid(prefix.length()).split(QLatin1Char(':'));
        if (parts.size() == 3)
            emit tileImageError(
                    product,
                    parts[0].toInt(), // z
                    parts[1].toInt(), // x
                    parts[2].toInt(), // y
                    message
            );
    }
}

// ─── gridToImage ─────────────────────────────────────────────────────────────
// Encodes each float value as a palette UV [0, 1] stored in Grayscale8.
// The fragment shader samples the palette strip texture using this UV directly.
//
// Encoding:  uv = clamp((v - offset) * scale / (numSteps - 1), 0, 1)
// Non-finite values (NaN, ±inf) map to 0.

QImage GridTileCache::gridToImage(const QVector<QVector<float>> &grid, float paletteScale, float paletteOffset, int numSteps) {
    if (grid.isEmpty() || grid[0].isEmpty())
        return QImage();

    const int rows = grid.size();
    const int cols = grid[0].size();
    const float stepRange = float(std::max(numSteps - 1, 1));

    QImage img(cols, rows, QImage::Format_Grayscale8);
    for (int r = 0; r < rows; ++r) {
        uchar *line = img.scanLine(r);
        for (int c = 0; c < cols; ++c) {
            const float v = grid[r][c];
            if (!std::isfinite(v)) {
                line[c] = 0;
            } else {
                const float idx = (v - paletteOffset) * paletteScale;
                const float uv = std::clamp(idx / stepRange, 0.0f, 1.0f);
                line[c] = static_cast<uchar>(uv * 255.0f + 0.5f);
            }
        }
    }
    return img;
}

// ─── Cache key helpers ────────────────────────────────────────────────────────

QString GridTileCache::tileKey(const QString &product, int z, int x, int y) {
    return product + QLatin1Char(':') + QString::number(z) + QLatin1Char(':') + QString::number(x) + QLatin1Char(':') + QString::number(y);
}

QString GridTileCache::diskPath(const QString &key) const {
    QString safe = key;
    safe.replace(QLatin1Char(':'), QLatin1Char('_')).replace(QLatin1Char('/'), QLatin1Char('_')).replace(QLatin1Char('\\'), QLatin1Char('_'));
    return m_diskCacheDir + QLatin1Char('/') + safe + QStringLiteral(".png");
}

bool GridTileCache::loadFromDisk(const QString &key, QImage &out) const {
    const QString path = diskPath(key);
    if (!QFileInfo::exists(path))
        return false;
    QImage img(path);
    if (img.isNull())
        return false;
    out = img;
    return true;
}

void GridTileCache::saveToDisk(const QString &key, const QImage &image) const {
    if (!image.save(diskPath(key), "PNG"))
        qWarning("GridTileCache: failed to save disk cache entry %s", qPrintable(key));
}
