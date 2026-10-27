#version 440

// Squeeze: the outgoing picture is squeezed away as the incoming one stretches in

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    float tve_progress;
    int orientation;
};

layout(binding = 1) uniform sampler2D tve_from;
layout(binding = 2) uniform sampler2D tve_to;

// Pictures are transparent outside their edges.
vec4 fromAt(vec2 uv)
{
    return any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) ? vec4(0.0) : texture(tve_from, uv);
}

vec4 toAt(vec2 uv)
{
    return any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) ? vec4(0.0) : texture(tve_to, uv);
}

void main()
{
    float p = clamp(smoothstep(0.0, 1.0, tve_progress), 0.0001, 0.9999);
    float x = orientation == 0 ? v_uv.x : v_uv.y;
    float split = 1.0 - p;
    vec2 uv = v_uv;
    if (x < split) {
        float u = x / split;
        uv = orientation == 0 ? vec2(u, v_uv.y) : vec2(v_uv.x, u);
        fragColor = fromAt(uv);
    } else {
        float u = (x - split) / p;
        uv = orientation == 0 ? vec2(u, v_uv.y) : vec2(v_uv.x, u);
        fragColor = toAt(uv);
    }
}
