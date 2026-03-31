#version 440

layout(location = 0) in  vec2 qt_TexCoord;
layout(location = 0) out vec4 fragColor;

// Data texture: Grayscale8.
// Each pixel stores the pre-computed palette UV [0, 1], derived on the CPU by:
//   uv = clamp((gridValue - offset) * scale / (numSteps - 1), 0, 1)
layout(binding = 1) uniform sampler2D gridTexture;

// Palette strip: RGBA8, 256×1 pixels, pre-baked by PaletteManager.
// U coordinate = palette UV from gridTexture; V = 0.5 (single row).
layout(binding = 2) uniform sampler2D paletteTexture;

layout(std140, binding = 0) uniform buf {
    mat4  qt_Matrix;
    float qt_Opacity;
    float _pad[3];
} ubuf;

void main()
{
    // The data texture pixel already encodes the normalised palette position.
    float uv = texture(gridTexture, qt_TexCoord).r;

    // Look up the RGBA colour from the palette strip.
    vec4 c = texture(paletteTexture, vec2(uv, 0.5));

    // Qt Quick compositor uses premultiplied alpha (src_factor = One).
    float alpha = c.a * ubuf.qt_Opacity;
    fragColor = vec4(c.rgb * alpha, alpha);
}
