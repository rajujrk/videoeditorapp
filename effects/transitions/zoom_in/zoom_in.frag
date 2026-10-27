#version 440

// Zoom In: the outgoing picture zooms in and dissolves into the incoming one

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    float tve_progress;
    float strength;
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
    float p = tve_progress;
    vec2 fromUv = (v_uv - 0.5) / (1.0 + strength * p) + 0.5;
    vec2 toUv = (v_uv - 0.5) / (1.0 + strength * 0.25 * (1.0 - p)) + 0.5;
    fragColor = mix(fromAt(fromUv), toAt(toUv), smoothstep(0.2, 0.8, p));
}
