#include "PaletteManager.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <algorithm>
#include <cmath>

// ─── Construction ─────────────────────────────────────────────────────────────

PaletteManager::PaletteManager()
{
    QFile f(QStringLiteral(":/data/palettes.json"));
    if (f.open(QIODevice::ReadOnly))
        loadFromBytes(f.readAll(), QStringLiteral(":/data/palettes.json"));
}

// ─── reload ───────────────────────────────────────────────────────────────────

void PaletteManager::reload(const QStringList &searchPaths)
{
    m_palettes.clear();

    QFile f(QStringLiteral(":/data/palettes.json"));
    if (f.open(QIODevice::ReadOnly))
        loadFromBytes(f.readAll(), QStringLiteral(":/data/palettes.json"));

    for (const QString &dir : searchPaths) {
        const QString path = QDir(dir).filePath(QStringLiteral("palettes.json"));
        QFile ef(path);
        if (!ef.open(QIODevice::ReadOnly)) continue;
        qInfo("PaletteManager: merging '%s'", qPrintable(path));
        loadFromBytes(ef.readAll(), path);
    }
}

// ─── loadFromBytes ────────────────────────────────────────────────────────────

void PaletteManager::loadFromBytes(const QByteArray &data, const QString &src)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning("PaletteManager: JSON parse error in '%s': %s",
                 qPrintable(src), qPrintable(err.errorString()));
        return;
    }

    const QJsonObject palettes = doc.object()
                                    .value(QStringLiteral("palettes"))
                                    .toObject();

    for (auto it = palettes.constBegin(); it != palettes.constEnd(); ++it) {
        const QJsonObject pal  = it.value().toObject();
        const QJsonObject vti  = pal.value(QStringLiteral("valueToIndex")).toObject();
        const QJsonArray  steps = pal.value(QStringLiteral("steps")).toArray();

        if (steps.isEmpty()) {
            qWarning("PaletteManager: palette '%s' has no steps — skipped",
                     qPrintable(it.key()));
            continue;
        }

        PaletteInfo info;
        info.scale    = float(vti.value(QStringLiteral("scale" )).toDouble(1.0));
        info.offset   = float(vti.value(QStringLiteral("offset")).toDouble(0.0));
        info.numSteps = steps.size();
        info.image    = buildImage(steps, 256);

        m_palettes.insert(it.key(), info);
        qInfo("PaletteManager: loaded '%s'  steps=%d  scale=%.4f  offset=%.4f",
              qPrintable(it.key()), info.numSteps,
              double(info.scale), double(info.offset));
    }
}

// ─── Accessors ────────────────────────────────────────────────────────────────

const PaletteManager::PaletteInfo *PaletteManager::palette(const QString &name) const
{
    auto it = m_palettes.constFind(name);
    return (it != m_palettes.constEnd()) ? &it.value() : nullptr;
}

QStringList PaletteManager::names() const
{
    return QStringList(m_palettes.keyBegin(), m_palettes.keyEnd());
}

// ─── buildImage ───────────────────────────────────────────────────────────────
// Linearly interpolates the control-point steps into a 256×1 RGBA8888 strip.

QImage PaletteManager::buildImage(const QJsonArray &steps, int resolution)
{
    struct Step { float v, r, g, b, a; };

    QVector<Step> parsed;
    parsed.reserve(steps.size());
    for (const QJsonValue &jv : steps) {
        const QJsonObject obj = jv.toObject();
        parsed.append({
            float(obj.value(QStringLiteral("v")).toDouble()),
            float(obj.value(QStringLiteral("r")).toInt()) / 255.0f,
            float(obj.value(QStringLiteral("g")).toInt()) / 255.0f,
            float(obj.value(QStringLiteral("b")).toInt()) / 255.0f,
            float(obj.value(QStringLiteral("a")).toInt()) / 255.0f,
        });
    }

    const int n = parsed.size();
    QImage img(resolution, 1, QImage::Format_RGBA8888);
    uchar *line = img.scanLine(0);

    for (int i = 0; i < resolution; ++i) {
        const float t   = float(i) / float(resolution - 1);
        const float idx = t * float(n - 1);
        const int   lo  = int(idx);
        const int   hi  = std::min(lo + 1, n - 1);
        const float f   = idx - float(lo);

        const auto lerp = [f](float a, float b) { return a + (b - a) * f; };
        auto clamp01 = [](float v) -> uchar {
            return static_cast<uchar>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
        };

        line[i * 4 + 0] = clamp01(lerp(parsed[lo].r, parsed[hi].r));
        line[i * 4 + 1] = clamp01(lerp(parsed[lo].g, parsed[hi].g));
        line[i * 4 + 2] = clamp01(lerp(parsed[lo].b, parsed[hi].b));
        line[i * 4 + 3] = clamp01(lerp(parsed[lo].a, parsed[hi].a));
    }

    return img;
}
