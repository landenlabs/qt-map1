#version 440

// ─── Inputs from QSGGeometry::defaultAttributes_TexturedPoint2D() ────────────
layout(location = 0) in vec4 qt_Vertex;
layout(location = 1) in vec2 qt_MultiTexCoord0;

layout(location = 0) out vec2 qt_TexCoord;

// ─── Uniform buffer (binding 0) ──────────────────────────────────────────────
// Must match the layout in floatgrid.frag and FloatGridMaterial.cpp exactly.
//
//  offset  0 : mat4  qt_Matrix    (64 bytes) — scene-graph MVP
//  offset 64 : float qt_Opacity   ( 4 bytes) — item opacity
//  offset 68 : float _pad[3]      (12 bytes) — std140 pad to 80
//  total = 80 bytes

layout(std140, binding = 0) uniform buf {
    mat4  qt_Matrix;
    float qt_Opacity;
    float _pad[3];
} ubuf;

void main()
{
    gl_Position = ubuf.qt_Matrix * qt_Vertex;
    qt_TexCoord = qt_MultiTexCoord0;
}
