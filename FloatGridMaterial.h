#pragma once

#include <QSGMaterial>
#include <QSGMaterialShader>
#include <QSGTexture>

// ─── FloatGridShader ─────────────────────────────────────────────────────────
//
// QSGMaterialShader that:
//   • loads pre-compiled .qsb shaders from Qt resources
//   • fills the UBO at binding 0 (matrix, opacity, dataMin, dataMax)
//   • binds the float-data texture at sampler binding 1

class FloatGridShader : public QSGMaterialShader
{
public:
    FloatGridShader();

    bool updateUniformData(RenderState &state,
                           QSGMaterial *newMat,
                           QSGMaterial *oldMat) override;

    void updateSampledImage(RenderState &state,
                            int binding,
                            QSGTexture **texture,
                            QSGMaterial *newMat,
                            QSGMaterial *oldMat) override;
};

// ─── FloatGridMaterial ───────────────────────────────────────────────────────
//
// One material instance per rendered tile.
//
// texture        – Grayscale8 tile data; each pixel stores a pre-computed
//                  palette UV [0,1] derived from the raw float grid value via
//                  uv = clamp((v - offset) * scale / (numSteps-1), 0, 1).
//                  Owned by TileGridRootNode in OverlayItem.cpp.
//
// paletteTexture – 256×1 RGBA8888 colour strip pre-baked by PaletteManager.
//                  Shared across all tiles; owned by TileGridRootNode.

class FloatGridMaterial : public QSGMaterial
{
public:
    FloatGridMaterial();

    QSGMaterialType  *type()   const override;
    QSGMaterialShader *createShader(QSGRendererInterface::RenderMode) const override;

    // Used by the scene graph to decide whether two draw calls can be batched.
    int compare(const QSGMaterial *other) const override;

    QSGTexture *texture        = nullptr;  // non-owning, per-tile data
    QSGTexture *paletteTexture = nullptr;  // non-owning, shared palette strip
};
