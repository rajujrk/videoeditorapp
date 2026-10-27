#version 440

// Blur: the outgoing picture blurs away and the incoming one comes into focus

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    float tve_progress;
    float radius;
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
    float r = radius * sin(p * 3.14159265359);
    float mixAmount = smoothstep(0.3, 0.7, p);
    // 24 samples on a golden-angle spiral, plus the centre.
    vec4 sum = mix(texture(tve_from, v_uv), texture(tve_to, v_uv), mixAmount);
    for (int i = 1; i <= 24; ++i) {
        float t = float(i) / 24.0;
        float a = float(i) * 2.39996323;
        vec2 uv = v_uv + vec2(cos(a), sin(a)) * sqrt(t) * r * tve_texelSize;
        sum += mix(texture(tve_from, uv), texture(tve_to, uv), mixAmount);
    }
    fragColor = sum / 25.0;
}
