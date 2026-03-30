#version 440

layout(location = 0) in  vec2 qt_TexCoord;
layout(location = 0) out vec4 fragColor;

// Float-data texture: single-channel (R), values normalised to [0, 1].
// Backed by QImage::Format_Grayscale8; in phase 2 upgrade to R32F.
layout(binding = 1) uniform sampler2D gridTexture;

layout(std140, binding = 0) uniform buf {
    mat4  qt_Matrix;
    float qt_Opacity;
    float dataMin;   // raw float value that maps to colour 0
    float dataMax;   // raw float value that maps to colour 1
    float _pad;
} ubuf;

// ─── Viridis-inspired five-stop colormap ─────────────────────────────────────
// Maps t ∈ [0, 1] → RGB
//   0.00  dark purple
//   0.25  blue
//   0.50  teal/green
//   0.75  yellow
//   1.00  orange-red
vec3 viridis(float t)
{
    const vec3 c0 = vec3(0.267, 0.005, 0.329);
    const vec3 c1 = vec3(0.127, 0.566, 0.551);
    const vec3 c2 = vec3(0.369, 0.788, 0.383);
    const vec3 c3 = vec3(0.993, 0.906, 0.144);
    const vec3 c4 = vec3(0.988, 0.498, 0.012);

    if (t < 0.25) return mix(c0, c1, t * 4.0);
    if (t < 0.50) return mix(c1, c2, (t - 0.25) * 4.0);
    if (t < 0.75) return mix(c2, c3, (t - 0.50) * 4.0);
    return            mix(c3, c4, (t - 0.75) * 4.0);
}

void main()
{
    // Sample the single-channel float texture
    float raw = texture(gridTexture, qt_TexCoord).r;

    // Re-map from [dataMin, dataMax] to [0, 1] for the colormap.
    // For Grayscale8 storage dataMin = 0, dataMax = 1; the division is a no-op.
    // When we switch to R32F textures in phase 2, real physical units arrive here.
    float t = clamp(
        (raw - ubuf.dataMin) / max(ubuf.dataMax - ubuf.dataMin, 1e-6),
        0.0, 1.0);

    // Qt Quick's compositor uses premultiplied alpha blending (src_factor = One).
    // Output (R*A, G*A, B*A, A) so that colours composite correctly over the map.
    float alpha = ubuf.qt_Opacity;
    fragColor = vec4(viridis(t) * alpha, alpha);
}
