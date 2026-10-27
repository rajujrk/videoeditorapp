#version 440

// Ripple: waves run out from the centre while the pictures dissolve

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    float tve_progress;
    float amplitude;
    float waves;
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
    float aspect = tve_texelSize.y / tve_texelSize.x;
    float p = tve_progress;
    vec2 c = (v_uv - 0.5) * vec2(aspect, 1.0);
    float dist = length(c);
    vec2 dir = dist > 0.0 ? c / dist : vec2(0.0);
    float wave = sin(dist * waves * 6.28318530718 - p * 20.0) * amplitude * sin(p * 3.14159265359);
    vec2 uv = v_uv + dir * wave / vec2(aspect, 1.0);
    fragColor = mix(texture(tve_from, uv), texture(tve_to, uv), smoothstep(0.2, 0.8, p));
}
