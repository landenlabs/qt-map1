#include "FloatGridMaterial.h"

// ─── UBO layout ──────────────────────────────────────────────────────────────
// Must match shaders/floatgrid.vert and shaders/floatgrid.frag exactly.
//
//   offset  0 : mat4  qt_Matrix   64 bytes
//   offset 64 : float qt_Opacity   4 bytes
//   offset 68 : float dataMin      4 bytes
//   offset 72 : float dataMax      4 bytes
//   offset 76 : float _pad         4 bytes  (std140 tail-padding to reach 80)
//   total = 80 bytes

namespace {
constexpr int kMatrixOffset  =  0;
constexpr int kOpacityOffset = 64;
constexpr int kDataMinOffset = 68;
constexpr int kDataMaxOffset = 72;
constexpr int kUBOSize       = 80;
} // namespace

// ─── FloatGridShader ─────────────────────────────────────────────────────────

FloatGridShader::FloatGridShader()
{
    // qt_add_shaders with PREFIX "/shaders" + FILES "shaders/floatgrid.*"
    // produces paths of the form :/shaders/shaders/floatgrid.*.qsb
    setShaderFileName(VertexStage,
                      QStringLiteral(":/shaders/shaders/floatgrid.vert.qsb"));
    setShaderFileName(FragmentStage,
                      QStringLiteral(":/shaders/shaders/floatgrid.frag.qsb"));
}

bool FloatGridShader::updateUniformData(RenderState &state,
                                        QSGMaterial *newMat,
                                        QSGMaterial * /*oldMat*/)
{
    auto       *mat = static_cast<FloatGridMaterial *>(newMat);
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

    // Always upload custom uniforms; the material compare() drives batching.
    memcpy(buf->data() + kDataMinOffset, &mat->dataMin, 4);
    memcpy(buf->data() + kDataMaxOffset, &mat->dataMax, 4);
    changed = true;

    return changed;
}

void FloatGridShader::updateSampledImage(RenderState &state,
                                          int binding,
                                          QSGTexture **texture,
                                          QSGMaterial *newMat,
                                          QSGMaterial * /*oldMat*/)
{
    if (binding == 1) {
        QSGTexture *tex = static_cast<FloatGridMaterial *>(newMat)->texture.get();
        if (tex) {
            // commitTextureOperations uploads the QImage pixel data to the GPU.
            // Without this call the underlying QRhiTexture is never populated and
            // sampling returns zero → uniform dark colour with no variation.
            tex->commitTextureOperations(state.rhi(), state.resourceUpdateBatch());
            *texture = tex;
        }
    }
}

// ─── FloatGridMaterial ───────────────────────────────────────────────────────

FloatGridMaterial::FloatGridMaterial()
{
    setFlag(Blending, true);
}

QSGMaterialType *FloatGridMaterial::type() const
{
    static QSGMaterialType sType;
    return &sType;
}

QSGMaterialShader *FloatGridMaterial::createShader(QSGRendererInterface::RenderMode) const
{
    return new FloatGridShader();
}

int FloatGridMaterial::compare(const QSGMaterial *other) const
{
    const auto *o = static_cast<const FloatGridMaterial *>(other);
    const void *a = texture.get();
    const void *b = o->texture.get();
    return (a < b) ? -1 : (a > b) ? 1 : 0;
}
