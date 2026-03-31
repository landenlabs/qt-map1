#include "GridManager.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

// ─── Helpers ──────────────────────────────────────────────────────────────────

static QString jsonString(const QJsonObject &obj, const QString &key,
                          const QString &def = QString())
{
    const QJsonValue v = obj.value(key);
    if (v.isString()) {
        const QString s = v.toString().trimmed();
        if (!s.isEmpty()) return s;
    }
    return def;
}

static int jsonInt(const QJsonObject &obj, const QString &key, int def)
{
    const QJsonValue v = obj.value(key);
    return v.isDouble() ? v.toInt(def) : def;
}

// ─── Construction ─────────────────────────────────────────────────────────────

GridManager::GridManager(const QString &apiKey, QObject *parent)
    : QObject(parent), m_apiKey(apiKey)
{
    QFile f(QStringLiteral(":/data/grids.json"));
    if (f.open(QIODevice::ReadOnly))
        m_grids = parseJson(f.readAll(), QStringLiteral(":/data/grids.json"));
    rebuildVariant();
}

// ─── reload ───────────────────────────────────────────────────────────────────

void GridManager::reload(const QStringList &searchPaths)
{
    m_grids.clear();

    QFile f(QStringLiteral(":/data/grids.json"));
    if (f.open(QIODevice::ReadOnly))
        m_grids = parseJson(f.readAll(), QStringLiteral(":/data/grids.json"));

    for (const QString &dir : searchPaths) {
        const QString path = QDir(dir).filePath(QStringLiteral("grids.json"));
        QFile ef(path);
        if (!ef.open(QIODevice::ReadOnly)) continue;
        const QVector<GridDef> extra = parseJson(ef.readAll(), path);
        for (const GridDef &def : extra)
            mergeGrid(def);
    }

    rebuildVariant();
    emit gridsChanged();
}

// ─── mergeGrid ────────────────────────────────────────────────────────────────

void GridManager::mergeGrid(const GridDef &def)
{
    for (GridDef &existing : m_grids) {
        if (existing.name == def.name) {
            existing = def;
            return;
        }
    }
    m_grids.append(def);
}

// ─── rebuildVariant ───────────────────────────────────────────────────────────

void GridManager::rebuildVariant()
{
    m_gridsVariant.clear();
    for (const GridDef &gd : std::as_const(m_grids)) {
        m_gridsVariant.append(QVariantMap{
            { "name",        gd.name        },
            { "prodCode",    gd.prodCode    },
            { "prodName",    gd.prodName    },
            { "product",     gd.product     },
            { "type",        gd.type        },
            { "comment",     gd.comment     },
            { "maxLod",      gd.maxLod      },
            { "hasTiming",   gd.hasTiming   },
            { "urlInfo",     gd.urlInfo     },
            { "urlData",     gd.urlData     },
            { "paletteName", gd.paletteName },
        });
    }
}

// ─── QML property ─────────────────────────────────────────────────────────────

QVariantList GridManager::grids() const { return m_gridsVariant; }

// ─── JSON parsing ─────────────────────────────────────────────────────────────

QVector<GridManager::GridDef> GridManager::parseJson(const QByteArray &data,
                                                      const QString &src)
{
    QVector<GridDef> result;

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning("GridManager: JSON parse error in '%s': %s",
                 qPrintable(src), qPrintable(err.errorString()));
        return result;
    }
    if (!doc.isObject()) {
        qWarning("GridManager: '%s' root must be a JSON object", qPrintable(src));
        return result;
    }

    const QJsonObject root = doc.object();

    // ── Top-level defaults ────────────────────────────────────────────────────
    GridDef defaults;
    defaults.type    = jsonString(root, "type",    QStringLiteral("float4"));
    defaults.urlData = jsonString(root, "urldata");
    defaults.urlTm   = jsonString(root, "utltm");
    defaults.urlInfo = jsonString(root, "urlinfo");
    defaults.maxLod  = jsonInt   (root, "maxLod",  2);

    // ── Grid items ────────────────────────────────────────────────────────────
    const QJsonArray items = root.value(QStringLiteral("grids")).toArray();
    if (items.isEmpty()) {
        qWarning("GridManager: no 'grids' array found in '%s'", qPrintable(src));
        return result;
    }

    for (const QJsonValue &v : items) {
        if (!v.isObject()) continue;
        const QJsonObject obj = v.toObject();

        GridDef gd;
        gd.name     = jsonString(obj, "name");
        gd.prodCode = jsonString(obj, "prodCode");
        gd.prodName = jsonString(obj, "prodName");
        if (gd.name.isEmpty() || gd.prodCode.isEmpty()) {
            qWarning("GridManager: skipping grid entry missing 'name' or 'prodCode'");
            continue;
        }

        gd.type        = jsonString(obj, "type",        defaults.type);
        gd.urlData     = jsonString(obj, "urldata",     defaults.urlData);
        gd.urlTm       = jsonString(obj, "utltm",       defaults.urlTm);
        gd.urlInfo     = jsonString(obj, "urlinfo",     defaults.urlInfo);
        gd.comment     = jsonString(obj, "comment");
        gd.paletteName = jsonString(obj, "paletteName");
        gd.maxLod      = jsonInt   (obj, "maxLod",      defaults.maxLod);

        gd.hasTiming = !gd.urlTm.isEmpty();
        gd.product   = gd.prodCode + QLatin1Char(':') + gd.prodName;

        if (gd.urlData.isEmpty()) {
            qWarning("GridManager: skipping '%s' — no urldata",
                     qPrintable(gd.name));
            continue;
        }

        result.append(gd);
    }

    qInfo("GridManager: loaded %d grid(s) from '%s'",
          (int)result.size(), qPrintable(src));
    return result;
}

// ─── Helpers ──────────────────────────────────────────────────────────────────

QString GridManager::substituteKP(const QString &tmpl, const QString &product) const
{
    return QString(tmpl)
        .replace("{k}", m_apiKey)
        .replace("{p}", product);
}

// ─── Grid enable / disable ────────────────────────────────────────────────────

void GridManager::enableGrid(int index)
{
    if (index < 0 || index >= (int)m_grids.size()) return;
    const GridDef &gd = m_grids[index];

    const QString endpoint = substituteKP(gd.urlData, gd.product);

    qInfo("GridManager: grid %d '%s' enabled", index, qPrintable(gd.name));
    emit gridReady(index, endpoint);
}

void GridManager::disableGrid(int index)
{
    Q_UNUSED(index)
}
