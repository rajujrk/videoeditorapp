#version 440

// Pixelate: the picture breaks into blocks and rebuilds as the incoming one

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    float tve_progress;
    float blockSize;
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
    float size = max(1.0, blockSize * (1.0 - abs(2.0 * p - 1.0)));
    vec2 block = tve_texelSize * size;
    vec2 uv = (floor(v_uv / block) + 0.5) * block;
    fragColor = mix(fromAt(uv), toAt(uv), smoothstep(0.4, 0.6, p));
}
