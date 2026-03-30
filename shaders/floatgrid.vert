#version 440

// ─── Inputs from QSGGeometry::defaultAttributes_TexturedPoint2D() ────────────
// vec4 position matches what Qt's batch renderer expects (location 0).
// qsb --batchable injects an extra _qt_order attribute at location 7 and
// rewrites gl_Position.z — we don't add that manually; it's automatic.
layout(location = 0) in vec4 qt_Vertex;
layout(location = 1) in vec2 qt_MultiTexCoord0;

layout(location = 0) out vec2 qt_TexCoord;

// ─── Uniform buffer (binding 0) ──────────────────────────────────────────────
// Must match the layout in floatgrid.frag and FloatGridMaterial.cpp exactly.
//
//  offset  0 : mat4  qt_Matrix   (64 bytes)  — scene-graph MVP
//  offset 64 : float qt_Opacity  ( 4 bytes)  — item opacity
//  offset 68 : float dataMin     ( 4 bytes)  — float range lo
//  offset 72 : float dataMax     ( 4 bytes)  — float range hi
//  offset 76 : float _pad        ( 4 bytes)  — std140 alignment pad
//  total = 80 bytes

layout(std140, binding = 0) uniform buf {
    mat4  qt_Matrix;
    float qt_Opacity;
    float dataMin;
    float dataMax;
    float _pad;
} ubuf;

void main()
{
    gl_Position = ubuf.qt_Matrix * qt_Vertex;
    qt_TexCoord = qt_MultiTexCoord0;
}
