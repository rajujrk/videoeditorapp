#version 440

// Shared vertex shader for every effect pass: a four-vertex triangle strip covering the
// whole target, no vertex buffer. Declares only the standard header of EffectParams.

layout(location = 0) out vec2 v_uv;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
};

void main()
{
    vec2 p = vec2(float(gl_VertexIndex & 1), float((gl_VertexIndex >> 1) & 1));
    v_uv = vec2(p.x, tve_flipY > 0.5 ? 1.0 - p.y : p.y);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
