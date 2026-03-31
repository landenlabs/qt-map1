#include "GridTileCache.h"
#include "GridLoader.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>
#include <limits>

// ─── Construction ─────────────────────────────────────────────────────────────

GridTileCache::GridTileCache(const QString &apiKey,
                             qsizetype maxMemBytes,
                             QObject *parent)
    : QObject(parent)
    , m_loader(new GridLoader(apiKey, this))
    , m_memCache(maxMemBytes)
{
    m_diskCacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                   + QStringLiteral("/grid_tiles");
    QDir().mkpath(m_diskCacheDir);
    qInfo("GridTileCache: disk cache → %s", qPrintable(m_diskCacheDir));

    connect(m_loader, &GridLoader::tileReady,
            this,     &GridTileCache::onTileReady);
    connect(m_loader, &GridLoader::tileError,
            this,     &GridTileCache::onTileError);
}

// ─── requestTileImage ─────────────────────────────────────────────────────────

void GridTileCache::requestTileImage(const QString &product, const QString &type,
                                     int z, int x, int y)
{
    const QString key = tileKey(product, z, x, y);

    // 1. Memory cache hit
    if (QImage *cached = m_memCache[key]) {
        qInfo("GridTileCache: mem-cache hit  %s", qPrintable(key));
        emit tileImageReady(product, z, x, y, *cached);
        return;
    }

    // 2. Disk cache hit
    QImage diskImg;
    if (loadFromDisk(key, diskImg)) {
        qInfo("GridTileCache: disk-cache hit %s", qPrintable(key));
        m_memCache.insert(key, new QImage(diskImg),
                          static_cast<qsizetype>(diskImg.sizeInBytes()));
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
    m_loader->fetchTile(product, type, x, y, z);
}

// ─── onTileReady ─────────────────────────────────────────────────────────────

void GridTileCache::onTileReady(const QString &product, int x, int y, int z,
                                const QVector<QVector<float>> &grid)
{
    const QString key = tileKey(product, z, x, y);
    m_inFlight.remove(key);

    const QImage img = gridToImage(grid);
    if (img.isNull()) {
        emit tileImageError(product, z, x, y,
                            QStringLiteral("gridToImage produced null image"));
        return;
    }

    saveToDisk(key, img);
    m_memCache.insert(key, new QImage(img),
                      static_cast<qsizetype>(img.sizeInBytes()));

    qInfo("GridTileCache: cached %s  (%dx%d)", qPrintable(key), img.width(), img.height());
    emit tileImageReady(product, z, x, y, img);
}

// ─── onTileError ─────────────────────────────────────────────────────────────
// GridLoader::tileError only carries product + message (not x/y/z).
// Find all in-flight keys for this product, parse their coordinates, and
// emit a tileImageError for each.

void GridTileCache::onTileError(const QString &product, const QString &message)
{
    const QString prefix = product + QLatin1Char(':');
    QList<QString> toRemove;
    for (const QString &key : m_inFlight)
        if (key.startsWith(prefix))
            toRemove.append(key);

    for (const QString &key : toRemove) {
        m_inFlight.remove(key);
        // key format: "product:z:x:y"
        const QStringList parts = key.mid(prefix.length()).split(QLatin1Char(':'));
        if (parts.size() == 3)
            emit tileImageError(product,
                                parts[0].toInt(),   // z
                                parts[1].toInt(),   // x
                                parts[2].toInt(),   // y
                                message);
    }
}

// ─── gridToImage ─────────────────────────────────────────────────────────────
// Normalise the float grid to [0,255] using per-tile min/max.
// Non-finite values (NaN, ±inf / missing_value) map to 0 (black).

QImage GridTileCache::gridToImage(const QVector<QVector<float>> &grid)
{
    if (grid.isEmpty() || grid[0].isEmpty())
        return QImage();

    const int rows = grid.size();
    const int cols = grid[0].size();

    float minVal =  std::numeric_limits<float>::max();
    float maxVal = -std::numeric_limits<float>::max();
    for (const auto &row : grid)
        for (float v : row)
            if (std::isfinite(v)) {
                if (v < minVal) minVal = v;
                if (v > maxVal) maxVal = v;
            }

    if (minVal >= maxVal)
        maxVal = minVal + 1.0f;   // degenerate tile: constant value
    const float range = maxVal - minVal;

    QImage img(cols, rows, QImage::Format_Grayscale8);
    for (int r = 0; r < rows; ++r) {
        uchar *line = img.scanLine(r);
        for (int c = 0; c < cols; ++c) {
            const float v = grid[r][c];
            if (!std::isfinite(v)) {
                line[c] = 0;
            } else {
                const float n = std::clamp((v - minVal) / range, 0.0f, 1.0f);
                line[c] = static_cast<uchar>(n * 255.0f + 0.5f);
            }
        }
    }
    return img;
}

// ─── Cache key helpers ────────────────────────────────────────────────────────

QString GridTileCache::tileKey(const QString &product, int z, int x, int y)
{
    return product
         + QLatin1Char(':') + QString::number(z)
         + QLatin1Char(':') + QString::number(x)
         + QLatin1Char(':') + QString::number(y);
}

QString GridTileCache::diskPath(const QString &key) const
{
    QString safe = key;
    // Sanitise characters that are unsafe in filenames
    safe.replace(QLatin1Char(':'),  QLatin1Char('_'))
        .replace(QLatin1Char('/'),  QLatin1Char('_'))
        .replace(QLatin1Char('\\'), QLatin1Char('_'));
    return m_diskCacheDir + QLatin1Char('/') + safe + QStringLiteral(".png");
}

bool GridTileCache::loadFromDisk(const QString &key, QImage &out) const
{
    const QString path = diskPath(key);
    if (!QFileInfo::exists(path))
        return false;
    QImage img(path);
    if (img.isNull())
        return false;
    out = img;
    return true;
}

void GridTileCache::saveToDisk(const QString &key, const QImage &image) const
{
    if (!image.save(diskPath(key), "PNG"))
        qWarning("GridTileCache: failed to save disk cache entry %s", qPrintable(key));
}
