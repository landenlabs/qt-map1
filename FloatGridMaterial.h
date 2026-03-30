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
// One material instance per rendered tile.  Owns the QSGTexture that backs
// the float grid data and exposes the scalar range [dataMin, dataMax] that
// the fragment shader uses to normalise values before colour-mapping.

class FloatGridMaterial : public QSGMaterial
{
public:
    FloatGridMaterial();

    QSGMaterialType  *type()   const override;
    QSGMaterialShader *createShader(QSGRendererInterface::RenderMode) const override;

    // Used by the scene graph to decide whether two draw calls can be batched.
    int compare(const QSGMaterial *other) const override;

    // Non-owning pointer — texture is owned by TileGridRootNode in OverlayItem.cpp
    QSGTexture *texture = nullptr;
    float dataMin = 0.0f;
    float dataMax = 1.0f;
};
