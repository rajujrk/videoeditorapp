#version 440

// Dip to White: fades out to a colour, then in from it

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    float tve_progress;
    vec4 color;
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
    vec4 dip = vec4(color.rgb, 1.0);
    float p = tve_progress;
    fragColor = p < 0.5 ? mix(fromAt(v_uv), dip, smoothstep(0.0, 1.0, p * 2.0))
                        : mix(dip, toAt(v_uv), smoothstep(0.0, 1.0, p * 2.0 - 1.0));
}
