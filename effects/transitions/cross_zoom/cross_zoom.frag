#version 440

// Cross Zoom: a zoom blur rushes through the cut

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
    float amount = strength * sin(p * 3.14159265359);
    float mixAmount = smoothstep(0.35, 0.65, p);
    vec4 sum = vec4(0.0);
    for (int i = 0; i < 16; ++i) {
        vec2 uv = mix(v_uv, vec2(0.5), amount * float(i) / 15.0);
        sum += mix(texture(tve_from, uv), texture(tve_to, uv), mixAmount);
    }
    fragColor = sum / 16.0;
}
