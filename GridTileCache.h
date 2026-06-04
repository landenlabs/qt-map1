#pragma once

#include <QObject>
#include <QCache>
#include <QHash>
#include <QImage>
#include <QSet>
#include <QString>
#include <QVector>

class GridLoader;
class QThread;

// GridTileCache – converts fetched float-grid tiles into palette-indexed
// Grayscale8 QImages and caches them in memory and on disk.
//
// Each pixel in the output image stores a pre-computed palette UV [0, 1]:
//   uv = clamp((gridValue - paletteOffset) * paletteScale / (numSteps-1), 0, 1)
//
// The UV is baked into the texture so the fragment shader needs only to sample
// the palette strip — no per-pixel arithmetic at draw time.
//
// Memory cache: QCache limited to maxMemBytes (default 64 MB).
// Disk cache:   grid_tiles_p1/<key>.png  (p1 = palette-indexed format version 1)

class GridTileCache : public QObject
{
    Q_OBJECT

public:
    explicit GridTileCache(const QString &apiKey,
                           qsizetype maxMemBytes = 64 * 1024 * 1024,
                           QObject *parent = nullptr);
    ~GridTileCache() override;

    // Request the QImage for one tile.
    // paletteScale / paletteOffset / numSteps – from the active palette;
    //   used to encode the palette UV into the Grayscale8 output image.
    // urlInfo / urlData – endpoint templates from grids.json.
    void requestTileImage(const QString &product, const QString &type,
                          const QString &urlInfo,  const QString &urlData,
                          float paletteScale, float paletteOffset, int numSteps,
                          int z, int x, int y);

    // Unique cache key string: "product:z:x:y" — public so callers can key textures.
    static QString tileKey(const QString &product, int z, int x, int y);

    // When false, requestTileImage skips disk reads and onTileReady skips disk writes.
    // Memory cache is unaffected.
    void setDiskCacheEnabled(bool on);
    bool diskCacheEnabled() const { return m_diskCacheEnabled; }

    // Drop every entry from the in-memory QCache so subsequent requests fall
    // through to disk (if enabled) or the network.  Does not touch disk files.
    void clearMemoryCache();

    // Delete every file under the on-disk cache directory.  Does not touch the
    // in-memory cache.
    void clearDiskCache();

    // Current number of entries held in the in-memory QCache.
    int memoryCacheCount() const { return m_memCache.count(); }

    // Abort any in-flight tile fetch whose key is not in keepKeys.  Called from
    // OverlayItem::setVisibleTiles when the viewport changes — stops stale tiles
    // from hogging the network's per-host connection slots.
    void cancelOutsideOf(const QSet<QString> &keepKeys);

    // Diagnostic mode: skip mem/disk reads, parseFloat4, gridToImage, disk write,
    // and tileImageReady emission.  Used to isolate pure network throughput.
    void setLoadOnly(bool on);

signals:
    void tileImageReady(const QString &product, int z, int x, int y,
                        const QImage &image);
    void tileImageError(const QString &product, int z, int x, int y,
                        const QString &message);

    // Internal signals — dispatched as QueuedConnection to GridLoader on a
    // worker thread so HTTP I/O and reply handling don't run on the GUI thread.
    void fetchTileRequested(const QString &product, const QString &type,
                            const QString &urlInfo, const QString &urlData,
                            int x, int y, int z);
    void cancelTileRequested(const QString &key);

private:
    // Palette parameters stored per product so onTileReady can encode correctly.
    struct PaletteParams {
        float paletteScale;
        float paletteOffset;
        int   numSteps;
    };

    void onTileReady(const QString &product, int x, int y, int z,
                     const QVector<QVector<float>> &grid);
    void onTileError(const QString &product, const QString &message);

    // Encode a float grid as a Grayscale8 palette-UV image.
    // Each pixel = clamp((v - offset) * scale / (numSteps-1), 0, 1) * 255.
    // Non-finite values map to 0.
    static QImage gridToImage(const QVector<QVector<float>> &grid,
                              float paletteScale, float paletteOffset, int numSteps);

    QString diskPath(const QString &key) const;
    bool    loadFromDisk(const QString &key, QImage &out) const;
    void    saveToDisk  (const QString &key, const QImage &image) const;

    GridLoader                   *m_loader;
    QThread                      *m_loaderThread = nullptr;
    QCache<QString, QImage>       m_memCache;
    QString                       m_diskCacheDir;
    QSet<QString>                 m_inFlight;
    QHash<QString, PaletteParams> m_productPalette;  // product → palette params
    QSet<QString>                 m_productDirsCreated;  // subdirs we've mkpath'd
    bool                          m_diskCacheEnabled = true;
    bool                          m_loadOnly         = false;
};
