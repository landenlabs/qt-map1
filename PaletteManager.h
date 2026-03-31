#pragma once

#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QString>
#include <QStringList>

// PaletteManager – loads palettes.json from a Qt resource and pre-bakes each
// named palette into a 256×1 RGBA8888 QImage (colour strip).
//
// The colour strip is uploaded once as a QSGTexture and sampled by the
// floatgrid fragment shader.  The data texture fed to the shader stores
// pre-computed palette UV coordinates [0, 1] in Grayscale8, calculated by:
//
//     uv = clamp((gridValue - offset) * scale / (numSteps - 1), 0, 1)
//
// where scale, offset, and numSteps come from the palette's "valueToIndex"
// section in palettes.json.

class PaletteManager
{
public:
    struct PaletteInfo {
        float  scale;      // from palettes.json valueToIndex.scale
        float  offset;     // from palettes.json valueToIndex.offset
        int    numSteps;   // number of control-point steps
        QImage image;      // 256×1 RGBA8888 pre-baked colour strip
    };

    // resourcePath – Qt resource path, e.g. ":/data/palettes.json".
    explicit PaletteManager(const QString &resourcePath = QStringLiteral(":/data/palettes.json"));

    // Returns nullptr if the name is not found.
    const PaletteInfo *palette(const QString &name) const;

    QStringList names() const;

private:
    // Interpolate steps into a resolution-wide RGBA8888 QImage.
    static QImage buildImage(const QJsonArray &steps, int resolution = 256);

    QHash<QString, PaletteInfo> m_palettes;
};
