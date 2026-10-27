#version 440

// Split: the outgoing picture opens from the middle like doors

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    float tve_progress;
    int orientation;
    float softness;
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

vec4 sweep(float s, float p, float soft)
{
    float edge = p * (1.0 + soft);
    return mix(toAt(v_uv), fromAt(v_uv), smoothstep(edge - soft, edge, s));
}

void main()
{
    float s = abs((orientation == 0 ? v_uv.x : v_uv.y) - 0.5) * 2.0;
    fragColor = sweep(s, tve_progress, softness);
}
