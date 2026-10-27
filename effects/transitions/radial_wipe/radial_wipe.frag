#version 440

// Clock Wipe: a hand sweeps clockwise from twelve o'clock

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    float tve_progress;
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
    vec2 c = v_uv - 0.5;
    float angle = atan(c.x, -c.y); // 0 at the top, increasing clockwise
    float s = fract(angle / 6.28318530718 + 1.0);
    fragColor = sweep(s, tve_progress, softness);
}
