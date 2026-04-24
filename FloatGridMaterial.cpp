#include "FloatGridMaterial.h"

// ─── UBO layout ──────────────────────────────────────────────────────────────
// Must match shaders/floatgrid.vert and shaders/floatgrid.frag exactly.
//
//   offset  0 : mat4  qt_Matrix    64 bytes
//   offset 64 : float qt_Opacity    4 bytes
//   offset 68 : float _pad[3]      12 bytes  (std140: UBO must be 16-byte multiple)
//   total = 80 bytes

namespace {
constexpr int kMatrixOffset = 0;
constexpr int kOpacityOffset = 64;
constexpr int kUBOSize = 80;
} // namespace

// ─── FloatGridShader ─────────────────────────────────────────────────────────

FloatGridShader::FloatGridShader() {
    // qt_add_shaders with PREFIX "/shaders" + FILES "shaders/floatgrid.*"
    // produces paths of the form :/shaders/shaders/floatgrid.*.qsb
    setShaderFileName(VertexStage, QStringLiteral(":/shaders/shaders/floatgrid.vert.qsb"));
    setShaderFileName(FragmentStage, QStringLiteral(":/shaders/shaders/floatgrid.frag.qsb"));
}

/*
  Here is the call sequence:

  1. QML engine flags the scene graph dirty (e.g., after QQuickItem::update() or a node geometry change).
  2. The render thread runs a frame. The scene graph renderer walks every QSGGeometryNode that needs drawing.
  3. For each node, the renderer calls QSGMaterialShader::updateUniformData() to upload the UBO (matrix, opacity, etc.) to the GPU.
  4. For each sampler binding, it calls QSGMaterialShader::updateSampledImage() to bind the textures — this is where commitTextureOperations() is called to actually push the pixel data.

  So the chain is:

  QQuickItem::update()          ← you call this (main thread)
    → scene graph marks dirty
      → render thread frame
        → FloatGridShader::updateUniformData()   ← Qt calls this
        → FloatGridShader::updateSampledImage()  ← Qt calls this
*/
bool FloatGridShader::updateUniformData(RenderState &state, QSGMaterial * /*newMat*/, QSGMaterial * /*oldMat*/) {
    QByteArray *buf = state.uniformData();
    Q_ASSERT(buf->size() >= kUBOSize);

    bool changed = false;

    if (state.isMatrixDirty()) {
        const QMatrix4x4 m = state.combinedMatrix();
        memcpy(buf->data() + kMatrixOffset, m.constData(), 64);
        changed = true;
    }
    if (state.isOpacityDirty()) {
        const float op = state.opacity();
        memcpy(buf->data() + kOpacityOffset, &op, 4);
        changed = true;
    }

    return changed;
}

void FloatGridShader::updateSampledImage(RenderState &state, int binding, QSGTexture **texture, QSGMaterial *newMat, QSGMaterial * /*oldMat*/) {
    auto *mat = static_cast<FloatGridMaterial *>(newMat);

    if (binding == 1 && mat->texture) {
        mat->texture->commitTextureOperations(state.rhi(), state.resourceUpdateBatch());
        *texture = mat->texture;
    }
    if (binding == 2 && mat->paletteTexture) {
        mat->paletteTexture->commitTextureOperations(state.rhi(), state.resourceUpdateBatch());
        *texture = mat->paletteTexture;
    }
}

// ─── FloatGridMaterial ───────────────────────────────────────────────────────

FloatGridMaterial::FloatGridMaterial() {
    setFlag(Blending, true);
}

QSGMaterialType *FloatGridMaterial::type() const {
    static QSGMaterialType sType;
    return &sType;
}

QSGMaterialShader *FloatGridMaterial::createShader(QSGRendererInterface::RenderMode) const {
    return new FloatGridShader();
}

int FloatGridMaterial::compare(const QSGMaterial *other) const {
    const auto *o = static_cast<const FloatGridMaterial *>(other);
    const void *a = texture;
    const void *b = o->texture;
    return (a < b) ? -1 : (a > b) ? 1 : 0;
}
