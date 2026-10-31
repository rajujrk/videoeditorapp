#version 440

// Stabilization and flicker removal: each output pixel taken from where the shaky frame had
// it — zoomed in about the centre, then the frame's correction (a similarity: move, turn,
// scale) undone — and its colour multiplied by the frame's brightness gain. Worked out in
// pixels about the centre (so turns keep their shape); outside the frame: transparent.

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    vec4 move;   // x, y: correction shift (picture fractions), z: angle (radians), w: scale
    vec4 params; // x: zoom, y: gain
};

layout(binding = 1) uniform sampler2D tve_source;

void main()
{
    vec2 size = 1.0 / tve_texelSize;
    vec2 p = (v_uv - 0.5) * size / params.x - move.xy * size;
    float c = cos(move.z);
    float s = sin(move.z);
    vec2 raw = vec2(c * p.x + s * p.y, -s * p.x + c * p.y) / move.w;
    vec2 uv = raw / size + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) {
        fragColor = vec4(0.0);
        return;
    }
    vec4 colour = texture(tve_source, uv);
    fragColor = vec4(min(colour.rgb * params.y, vec3(1.0)), colour.a);
}
