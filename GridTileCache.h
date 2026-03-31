#pragma once

#include <QObject>
#include <QCache>
#include <QImage>
#include <QSet>
#include <QString>
#include <QVector>

class GridLoader;

// GridTileCache – converts fetched float-grid tiles into QImages and keeps
// previously loaded tiles in a two-level cache (memory + disk).
//
// Memory cache: QCache limited to maxMemBytes (default 64 MB).  Each entry
//   costs image.sizeInBytes() bytes; Qt evicts the least-recently-used tile
//   when the budget is exceeded.
//
// Disk cache: PNG files written to
//   QStandardPaths::CacheLocation/grid_tiles/<key>.png
//   so tiles survive app restarts without re-fetching.
//
// Usage:
//   cache.requestTileImage(product, type, z, x, y)
//   → tileImageReady(product, z, x, y, image)  emitted synchronously on a
//     cache hit, or asynchronously after the two-stage network fetch.
//   Duplicate in-flight requests for the same (product, z, x, y) are
//   coalesced — only one network fetch is issued.

class GridTileCache : public QObject
{
    Q_OBJECT

public:
    explicit GridTileCache(const QString &apiKey,
                           qsizetype maxMemBytes = 64 * 1024 * 1024,
                           QObject *parent = nullptr);

    // Request the QImage for one tile.
    // urlInfo / urlData – endpoint templates from grids.json (query string stripped
    // internally); passed through to GridLoader::fetchTile on a cache miss.
    void requestTileImage(const QString &product, const QString &type,
                          const QString &urlInfo, const QString &urlData,
                          int z, int x, int y);

    // Unique cache key string: "product:z:x:y" — public so callers can key textures.
    static QString tileKey(const QString &product, int z, int x, int y);

signals:
    void tileImageReady(const QString &product, int z, int x, int y,
                        const QImage &image);
    void tileImageError(const QString &product, int z, int x, int y,
                        const QString &message);

private:
    // Slots connected to the internal GridLoader
    void onTileReady(const QString &product, int x, int y, int z,
                     const QVector<QVector<float>> &grid);
    void onTileError(const QString &product, const QString &message);

    // Convert a 2-D float grid to a normalised Grayscale8 QImage.
    // Non-finite values (NaN, ±inf) are mapped to 0.
    // Values are linearly scaled from [min, max] → [0, 255].
    static QImage gridToImage(const QVector<QVector<float>> &grid);

    // Absolute path of the disk-cache file for this key.
    QString diskPath(const QString &key) const;

    bool loadFromDisk(const QString &key, QImage &out) const;
    void saveToDisk  (const QString &key, const QImage &image) const;

    GridLoader               *m_loader;
    QCache<QString, QImage>   m_memCache;    // key → image
    QString                   m_diskCacheDir;
    QSet<QString>             m_inFlight;    // keys with active network requests
};
