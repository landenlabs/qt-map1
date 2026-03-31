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
            { "product",   gd.product   },   // "prodCode:prodName" – used by GridTileCache
            { "type",      gd.type      },
            { "hasTiming", gd.hasTiming },
        });
    }
}

// ─── QML property ─────────────────────────────────────────────────────────────

QVariantList GridManager::grids() const { return m_gridsVariant; }

// ─── File parsing ─────────────────────────────────────────────────────────────

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
    if (!doc.isArray()) {
        qWarning("GridManager: '%s' must be a JSON array", qPrintable(path));
        return result;
    }

    for (const QJsonValue &v : doc.array()) {
        if (!v.isObject()) continue;
        const QJsonObject obj = v.toObject();

        GridDef gd;
        gd.name      = obj.value("name").toString().trimmed();
        gd.prodCode  = obj.value("prodCode").toString().trimmed();
        gd.prodName  = obj.value("prodName").toString().trimmed();
        gd.type      = obj.value("type").toString().trimmed();
        gd.urlData   = obj.value("urldata").toString().trimmed();
        gd.urlTm     = obj.value("utltm").toString().trimmed();
        gd.hasTiming = !gd.urlTm.isEmpty();
        gd.product   = gd.prodCode + ":" + gd.prodName;

        if (gd.name.isEmpty() || gd.urlData.isEmpty()) continue;

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
