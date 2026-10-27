#version 440

layout(location = 0) out vec2 v_texCoord;

layout(std140, binding = 0) uniform buf {
    mat4 yuvToRgb;
    vec4 scaleFlip; // xy: aspect-fit scale in NDC, z: 1 when NDC is y-up
};

void main()
{
    // Four-vertex triangle strip covering the unit square, no vertex buffer needed.
    vec2 p = vec2(float(gl_VertexIndex & 1), float((gl_VertexIndex >> 1) & 1));
    v_texCoord = vec2(p.x, scaleFlip.z > 0.5 ? 1.0 - p.y : p.y);
    gl_Position = vec4((p * 2.0 - 1.0) * scaleFlip.xy, 0.0, 1.0);
}
