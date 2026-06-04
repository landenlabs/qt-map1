#include "GridTileCache.h"
#include "GridLoader.h"
#include "PhaseStats.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMetaType>
#include <QStandardPaths>
#include <QThread>
#include <algorithm>
#include <cmath>

// ─── Construction ─────────────────────────────────────────────────────────────

GridTileCache::GridTileCache(const QString &apiKey, qsizetype maxMemBytes, QObject *parent)
    : QObject(parent)
    , m_loader(new GridLoader(apiKey))  // no parent — will be moved to worker thread
    , m_memCache(maxMemBytes) {
    // "p1" suffix = palette-indexed format v1; prevents stale per-tile-min/max
    // normalised tiles from being read by the new palette-UV encoding.
    m_diskCacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/grid_tiles_p1");
    QDir().mkpath(m_diskCacheDir);
    qInfo("GridTileCache: disk cache → %s", qPrintable(m_diskCacheDir));

    // Move GridLoader (and its QNAM pool) to a dedicated worker thread so HTTP
    // I/O scheduling and reply dispatch do not contend with the GUI event loop.
    qRegisterMetaType<QVector<QVector<float>>>("QVector<QVector<float>>");

    m_loaderThread = new QThread(this);
    m_loaderThread->setObjectName(QStringLiteral("GridLoader"));
    m_loader->moveToThread(m_loaderThread);
    connect(m_loaderThread, &QThread::finished, m_loader, &QObject::deleteLater);

    // Cache → loader: queued so the call hops to the worker thread.
    connect(this, &GridTileCache::fetchTileRequested,
            m_loader, &GridLoader::fetchTile, Qt::QueuedConnection);
    connect(this, &GridTileCache::cancelTileRequested,
            m_loader, &GridLoader::cancelTile, Qt::QueuedConnection);

    // Loader → cache: AutoConnection becomes Queued automatically across threads.
    connect(m_loader, &GridLoader::tileReady, this, &GridTileCache::onTileReady);
    connect(m_loader, &GridLoader::tileError, this, &GridTileCache::onTileError);

    m_loaderThread->start();
}

GridTileCache::~GridTileCache() {
    if (m_loaderThread) {
        m_loaderThread->quit();
        m_loaderThread->wait();
    }
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

    // Diagnostic load-only mode: bypass mem + disk reads so every request hits
    // the network.  Still honour m_inFlight dedup so we don't double-fire.
    if (m_loadOnly) {
        if (m_inFlight.contains(key)) {
            PhaseStats::instance().incr("coalesced");
            return;
        }
        PhaseStats::instance().incr("network_fetch");
        m_inFlight.insert(key);
        emit fetchTileRequested(product, type, urlInfo, urlData, x, y, z);
        return;
    }

    // 1. Memory cache hit
    if (QImage *cached = m_memCache[key]) {
        PhaseStats::instance().incr("mem_hit");
        emit tileImageReady(product, z, x, y, *cached);
        return;
    }

    // 2. Disk cache hit
    if (m_diskCacheEnabled) {
        QElapsedTimer t; t.start();
        QImage diskImg;
        const bool ok = loadFromDisk(key, diskImg);
        const qint64 ns = t.nsecsElapsed();
        if (ok) {
            PhaseStats::instance().record("disk_read_hit", ns);
            m_memCache.insert(key, new QImage(diskImg), static_cast<qsizetype>(diskImg.sizeInBytes()));
            emit tileImageReady(product, z, x, y, diskImg);
            return;
        } else {
            PhaseStats::instance().record("disk_read_miss", ns);
        }
    }

    // 3. Network fetch — coalesce duplicates
    if (m_inFlight.contains(key)) {
        PhaseStats::instance().incr("coalesced");
        return;
    }

    PhaseStats::instance().incr("network_fetch");
    m_inFlight.insert(key);
    emit fetchTileRequested(product, type, urlInfo, urlData, x, y, z);
}

// ─── onTileReady ─────────────────────────────────────────────────────────────

void GridTileCache::onTileReady(const QString &product, int x, int y, int z, const QVector<QVector<float>> &grid) {
    const QString key = tileKey(product, z, x, y);
    m_inFlight.remove(key);

    // Diagnostic load-only mode: GridLoader sent an empty grid.  Nothing to
    // decode, store, or emit — the network timing is already recorded.
    if (m_loadOnly || grid.isEmpty())
        return;

    // Retrieve palette params stored when the request was originally made.
    const PaletteParams pp = m_productPalette.value(product, PaletteParams{1.0f, 0.0f, 2});

    QImage img;
    {
        PhaseStats::Scope s("grid_to_image");
        img = gridToImage(grid, pp.paletteScale, pp.paletteOffset, pp.numSteps);
    }
    if (img.isNull()) {
        emit tileImageError(product, z, x, y, QStringLiteral("gridToImage produced null image"));
        return;
    }

    if (m_diskCacheEnabled) {
        PhaseStats::Scope s("disk_write");
        saveToDisk(key, img);
    }
    m_memCache.insert(key, new QImage(img), static_cast<qsizetype>(img.sizeInBytes()));

    qInfo("GridTileCache: cached %s  (%dx%d)", qPrintable(key), img.width(), img.height());
    emit tileImageReady(product, z, x, y, img);

    // Periodic dump so stats are visible without needing app shutdown.
    static int sCachedCount = 0;
    if ((++sCachedCount % 30) == 0)
        PhaseStats::instance().dump();
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

// Disk layout: <cacheRoot>/<product>/<z>_<x>_<y>.png
// One subdirectory per product keeps each product's tiles isolated, which
// makes selective clearing easy and avoids piling thousands of files into a
// single directory.
QString GridTileCache::diskPath(const QString &key) const {
    const int firstColon = key.indexOf(QLatin1Char(':'));
    const QString product = (firstColon >= 0) ? key.left(firstColon) : key;
    const QString coords  = (firstColon >= 0) ? key.mid(firstColon + 1) : QString();

    auto sanitize = [](QString s) {
        s.replace(QLatin1Char(':'), QLatin1Char('_'))
         .replace(QLatin1Char('/'), QLatin1Char('_'))
         .replace(QLatin1Char('\\'), QLatin1Char('_'));
        return s;
    };

    return m_diskCacheDir + QLatin1Char('/') + sanitize(product)
         + QLatin1Char('/') + sanitize(coords) + QStringLiteral(".png");
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
    const QString path = diskPath(key);
    const QString dir  = QFileInfo(path).absolutePath();

    // mkpath once per product subdirectory.  m_productDirsCreated is mutable
    // bookkeeping; the operation itself is idempotent and cheap, but skipping
    // the call entirely is cheaper on the hot path.
    auto *self = const_cast<GridTileCache *>(this);
    if (!self->m_productDirsCreated.contains(dir)) {
        QDir().mkpath(dir);
        self->m_productDirsCreated.insert(dir);
    }

    if (!image.save(path, "PNG"))
        qWarning("GridTileCache: failed to save disk cache entry %s", qPrintable(key));
}

// ─── cancelOutsideOf ──────────────────────────────────────────────────────────

void GridTileCache::cancelOutsideOf(const QSet<QString> &keepKeys) {
    QList<QString> toCancel;
    for (const QString &k : m_inFlight)
        if (!keepKeys.contains(k))
            toCancel.append(k);

    if (toCancel.isEmpty())
        return;

    for (const QString &k : toCancel) {
        m_inFlight.remove(k);
        emit cancelTileRequested(k);
    }
    qInfo("GridTileCache: canceled %lld stale in-flight tile(s)", static_cast<long long>(toCancel.size()));
}

// ─── setDiskCacheEnabled ──────────────────────────────────────────────────────

void GridTileCache::setDiskCacheEnabled(bool on) {
    if (m_diskCacheEnabled == on)
        return;
    m_diskCacheEnabled = on;
    qInfo("GridTileCache: disk cache %s", on ? "enabled" : "disabled");
}

// ─── clearMemoryCache ─────────────────────────────────────────────────────────

void GridTileCache::clearMemoryCache() {
    const int count = m_memCache.count();
    m_memCache.clear();
    qInfo("GridTileCache: cleared in-memory cache (%d entries)", count);
}

// ─── clearDiskCache ───────────────────────────────────────────────────────────

void GridTileCache::clearDiskCache() {
    QDir dir(m_diskCacheDir);
    if (!dir.exists()) {
        qInfo("GridTileCache: disk cache dir does not exist, nothing to clear");
        return;
    }
    const bool ok = dir.removeRecursively();
    QDir().mkpath(m_diskCacheDir);
    m_productDirsCreated.clear();
    qInfo("GridTileCache: cleared disk cache at %s (%s)",
          qPrintable(m_diskCacheDir),
          ok ? "ok" : "with errors");
}

// ─── setLoadOnly ──────────────────────────────────────────────────────────────

void GridTileCache::setLoadOnly(bool on) {
    if (m_loadOnly == on)
        return;
    m_loadOnly = on;
    if (m_loader)
        m_loader->setLoadOnly(on);
    qInfo("GridTileCache: load-only diagnostic mode %s", on ? "ENABLED" : "disabled");
}
