#pragma once

#include <QObject>
#include <QCache>
#include <QHash>
#include <QImage>
#include <QSet>
#include <QString>
#include <QVector>

class GridLoader;

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

signals:
    void tileImageReady(const QString &product, int z, int x, int y,
                        const QImage &image);
    void tileImageError(const QString &product, int z, int x, int y,
                        const QString &message);

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
    QCache<QString, QImage>       m_memCache;
    QString                       m_diskCacheDir;
    QSet<QString>                 m_inFlight;
    QHash<QString, PaletteParams> m_productPalette;  // product → palette params
};
