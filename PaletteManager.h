#pragma once

#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QString>
#include <QStringList>

// PaletteManager – loads palettes.json from a Qt resource and pre-bakes each
// named palette into a 256×1 RGBA8888 QImage (colour strip).
//
// Data loading:
//   1. Built-in defaults are compiled into the app as :/data/palettes.json.
//   2. reload(searchPaths) re-reads the defaults then merges any palettes.json
//      found in each search directory.  Duplicate palette names in external
//      files replace the built-in entry.

class PaletteManager
{
public:
    struct PaletteInfo {
        float  scale;      // from palettes.json valueToIndex.scale
        float  offset;     // from palettes.json valueToIndex.offset
        int    numSteps;   // number of control-point steps
        QImage image;      // 256×1 RGBA8888 pre-baked colour strip
    };

    // Loads built-in palettes from :/data/palettes.json.
    PaletteManager();

    // Reloads from the built-in resource then merges any palettes.json found
    // in each search directory.  Duplicate names in external files replace
    // the built-in entry.
    void reload(const QStringList &searchPaths);

    // Returns nullptr if the name is not found.
    const PaletteInfo *palette(const QString &name) const;

    QStringList names() const;

private:
    static QImage buildImage(const QJsonArray &steps, int resolution = 256);
    void loadFromBytes(const QByteArray &data, const QString &src);

    QHash<QString, PaletteInfo> m_palettes;
};
