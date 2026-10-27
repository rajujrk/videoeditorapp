#version 440

// Wipe: a straight edge sweeps the incoming picture across

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    float tve_progress;
    int direction;
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

// Motion direction in picture space (y points down): left, right, up, down.
vec2 directionVector(int d)
{
    if (d == 1) return vec2(1.0, 0.0);
    if (d == 2) return vec2(0.0, -1.0);
    if (d == 3) return vec2(0.0, 1.0);
    return vec2(-1.0, 0.0);
}

vec4 sweep(float s, float p, float soft)
{
    float edge = p * (1.0 + soft);
    return mix(toAt(v_uv), fromAt(v_uv), smoothstep(edge - soft, edge, s));
}

void main()
{
    vec2 d = directionVector(direction);
    float s = dot(v_uv - 0.5, d) + 0.5; // 0 where the edge starts (the side it moves away from)
    fragColor = sweep(s, tve_progress, softness);
}
