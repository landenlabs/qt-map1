#pragma once

#include <QObject>
#include <QVariantList>
#include <QString>
#include <QVector>

// GridManager – loads float-grid overlay definitions from grids.json and
// handles enabling/disabling each grid overlay.
//
// grids.json structure:
//   Top-level object holds defaults applied to every grid item:
//     type, maxLod, urldata, utltm, urlinfo
//   "grids" array holds individual items; any field present on an item
//   overrides the corresponding top-level default.
//
//   Per-item required fields: name, prodCode, prodName
//   Per-item optional overrides: type, maxLod, urldata, utltm, urlinfo, comment
//
// QML usage:
//   gridManager.grids           – model for the Repeater (name, prodCode, …)
//   gridManager.enableGrid(i)   – call when toggle turns ON
//   gridManager.disableGrid(i)  – call when toggle turns OFF
//   Connections { target: gridManager
//     function onGridReady(i, endpoint) { ... }
//     function onGridError(i, msg)      { ... }
//   }

class GridManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList grids READ grids NOTIFY gridsChanged)

public:
    struct GridDef {
        QString name;
        QString prodCode;
        QString prodName;
        QString product;    // prodCode:prodName (derived)
        QString type;
        QString urlData;    // tile data template
        QString urlTm;      // timing info template (may be empty)
        QString urlInfo;    // metadata info template (may be empty)
        QString comment;    // human-readable description / notes
        bool    hasTiming = false;
        int     maxLod    = 2;
    };

    // gridsFilePath – primary path to grids.json (compile-time source path;
    //                 falls back to exe dir at runtime like LayerManager).
    // apiKey        – substituted for {k} in all URL templates.
    explicit GridManager(const QString &gridsFilePath,
                         const QString &apiKey,
                         QObject *parent = nullptr);

    QVariantList grids() const;

    Q_INVOKABLE void enableGrid(int index);
    Q_INVOKABLE void disableGrid(int index);

signals:
    void gridsChanged();

    // Emitted when the grid endpoint is ready.
    // endpoint has {k} and {p} already substituted; {rt},{t},{z},{x},{y} remain.
    void gridReady(int index, const QString &endpoint);

    void gridError(int index, const QString &errorMessage);

private:
    static QVector<GridDef> parseFile(const QString &path);
    QString substituteKP(const QString &tmpl, const QString &prodCode) const;

    QVector<GridDef> m_grids;
    QVariantList     m_gridsVariant;
    QString          m_apiKey;
};
