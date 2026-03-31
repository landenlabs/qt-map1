#include "GridManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

// ─── Construction ─────────────────────────────────────────────────────────────
/*
  Sample "grids.json"
    {
        "name": "GTemp",
        "prodCode": "1248",
        "prodName": "Temperaturesurface",
        "type": "float4",
        "urldata": "https://api.weather.com/v2/tiler/data?products={p}&rt={rt}&t={t}&lod={z}&x={x}&y={y}&apiKey={k}",
        "utltm": "https://api.weather.com/v2/tiler/info?products={p}&apiKey={k}"
    },
*/
GridManager::GridManager(const QString &gridsFilePath,
                          const QString &apiKey,
                          QObject *parent)
    : QObject(parent), m_apiKey(apiKey)
{
    // Search order: exe dir first (deployed / build run), then source-tree hint.
    const QString exeDir = QDir(QCoreApplication::applicationDirPath())
                               .filePath("grids.json");
    const QString path   = QFile::exists(exeDir) ? exeDir : gridsFilePath;

    m_grids = parseFile(path);

    for (const GridDef &gd : std::as_const(m_grids)) {
        m_gridsVariant.append(QVariantMap{
            { "name",      gd.name      },
            { "prodCode",  gd.prodCode  },
            { "prodName",  gd.prodName  },
            { "product",   gd.product   },
            { "type",      gd.type      },
            { "comment",   gd.comment   },
            { "maxLod",    gd.maxLod    },
            { "hasTiming", gd.hasTiming },
            { "urlInfo",   gd.urlInfo   },
            { "urlData",   gd.urlData   },
        });
    }
}

// ─── QML property ─────────────────────────────────────────────────────────────

QVariantList GridManager::grids() const { return m_gridsVariant; }

// ─── File parsing ─────────────────────────────────────────────────────────────

// Helper: return item value if present and non-empty, else the default value.
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

QVector<GridManager::GridDef> GridManager::parseFile(const QString &path)
{
    QVector<GridDef> result;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning("GridManager: cannot open '%s'", qPrintable(path));
        return result;
    }

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning("GridManager: JSON parse error in '%s': %s",
                 qPrintable(path), qPrintable(err.errorString()));
        return result;
    }
    if (!doc.isObject()) {
        qWarning("GridManager: '%s' root must be a JSON object", qPrintable(path));
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
        qWarning("GridManager: no 'grids' array found in '%s'", qPrintable(path));
        return result;
    }

    for (const QJsonValue &v : items) {
        if (!v.isObject()) continue;
        const QJsonObject obj = v.toObject();

        GridDef gd;
        // Required per-item fields
        gd.name     = jsonString(obj, "name");
        gd.prodCode = jsonString(obj, "prodCode");
        gd.prodName = jsonString(obj, "prodName");
        if (gd.name.isEmpty() || gd.prodCode.isEmpty()) {
            qWarning("GridManager: skipping grid entry missing 'name' or 'prodCode'");
            continue;
        }

        // Optional per-item overrides; fall back to top-level defaults
        gd.type    = jsonString(obj, "type",    defaults.type);
        gd.urlData = jsonString(obj, "urldata", defaults.urlData);
        gd.urlTm   = jsonString(obj, "utltm",   defaults.urlTm);
        gd.urlInfo = jsonString(obj, "urlinfo",  defaults.urlInfo);
        gd.comment = jsonString(obj, "comment");
        gd.maxLod  = jsonInt   (obj, "maxLod",  defaults.maxLod);

        gd.hasTiming = !gd.urlTm.isEmpty();
        gd.product   = gd.prodCode + QLatin1Char(':') + gd.prodName;

        if (gd.urlData.isEmpty()) {
            qWarning("GridManager: skipping '%s' — no urldata (no default either)",
                     qPrintable(gd.name));
            continue;
        }

        result.append(gd);
    }

    qInfo("GridManager: loaded %d grid(s) from '%s'",
          (int)result.size(), qPrintable(path));
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

    // Substitute {k} and {p}; leave {rt}, {t}, {z}, {x}, {y} for tile fetcher.
    const QString endpoint = substituteKP(gd.urlData, gd.product);

    qInfo("GridManager: grid %d '%s' enabled",
          index, qPrintable(gd.name));

    emit gridReady(index, endpoint);
}

void GridManager::disableGrid(int index)
{
    Q_UNUSED(index)
    // Overlay teardown handled in QML via gridManager.disableGrid signal chain.
}
