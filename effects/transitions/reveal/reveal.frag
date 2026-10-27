#version 440

// Reveal: the outgoing picture slides away, uncovering the incoming one

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    float tve_progress;
    int direction;
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

// Motion direction in picture space (y points down): left, right, up, down.
vec2 directionVector(int d)
{
    if (d == 1) return vec2(1.0, 0.0);
    if (d == 2) return vec2(0.0, -1.0);
    if (d == 3) return vec2(0.0, 1.0);
    return vec2(-1.0, 0.0);
}

void main()
{
    vec2 d = directionVector(direction);
    float p = smoothstep(0.0, 1.0, tve_progress);
    vec4 outgoing = fromAt(v_uv - d * p);
    fragColor = outgoing.a > 0.0 ? outgoing : toAt(v_uv);
}
