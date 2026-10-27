#version 440

// Iris: a circle opens from the centre

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
    float aspect = tve_texelSize.y / tve_texelSize.x;
    vec2 c = (v_uv - 0.5) * vec2(aspect, 1.0);
    float s = length(c) / length(vec2(aspect, 1.0) * 0.5); // 1 at the corners
    fragColor = sweep(s, tve_progress, softness);
}
