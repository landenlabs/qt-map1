#pragma once

#include <QObject>
#include <QVariantList>
#include <QString>
#include <QStringList>
#include <QVector>

// GridManager – loads float-grid overlay definitions from :/data/grids.json.
//
// Data loading:
//   1. Built-in defaults are compiled into the app as :/data/grids.json.
//   2. reload(searchPaths) re-reads the defaults then merges any grids.json
//      found in each search directory.  External entries with the same "name"
//      replace the built-in entry; new names are appended.
//   3. Emits gridsChanged() so QML Repeaters rebuild automatically.
//
// grids.json structure:
//   Top-level object holds defaults applied to every grid item:
//     type, maxLod, urldata, utltm, urlinfo
//   "grids" array holds individual items; any field present on an item
//   overrides the corresponding top-level default.
//
// QML usage:
//   gridManager.grids           – model for the Repeater
//   gridManager.enableGrid(i)   – call when toggle turns ON
//   gridManager.disableGrid(i)  – call when toggle turns OFF

class GridManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList grids READ grids NOTIFY gridsChanged)

public:
    struct GridDef {
        QString name;
        QString prodCode;
        QString prodName;
        QString product;
        QString type;
        QString urlData;
        QString urlTm;
        QString urlInfo;
        QString comment;
        QString paletteName;
        bool    hasTiming = false;
        int     maxLod    = 2;
    };

    explicit GridManager(const QString &apiKey, QObject *parent = nullptr);

    QVariantList grids() const;

    // Full reload: reset to resource defaults then merge any grids.json in
    // each search directory.  Duplicate names replace the built-in entry.
    // Emits gridsChanged().
    void reload(const QStringList &searchPaths);

    Q_INVOKABLE void enableGrid(int index);
    Q_INVOKABLE void disableGrid(int index);

signals:
    void gridsChanged();
    void gridReady(int index, const QString &endpoint);
    void gridError(int index, const QString &errorMessage);

private:
    static QVector<GridDef> parseJson(const QByteArray &data, const QString &src);
    void mergeGrid(const GridDef &def);
    void rebuildVariant();
    QString substituteKP(const QString &tmpl, const QString &product) const;

    QVector<GridDef> m_grids;
    QVariantList     m_gridsVariant;
    QString          m_apiKey;
};
