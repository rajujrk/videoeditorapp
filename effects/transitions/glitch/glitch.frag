#version 440

// Glitch: blocks jump and colours split, the cut flickering between the pictures.

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

const float PI = 3.14159265359;

// Pictures are transparent outside their edges.
vec4 fromAt(vec2 uv)
{
    return any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) ? vec4(0.0) : texture(tve_from, uv);
}
vec4 toAt(vec2 uv)
{
    return any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) ? vec4(0.0) : texture(tve_to, uv);
}
// Width / height and 1: multiply a uv offset by it for round shapes.
vec2 aspect() { return vec2(tve_texelSize.y / tve_texelSize.x, 1.0); }
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), f.x), mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), f.x), f.y);
}
// Straight-alpha `top` over `bottom`.
vec4 over(vec4 top, vec4 bottom)
{
    float a = top.a + bottom.a * (1.0 - top.a);
    return vec4((top.rgb * top.a + bottom.rgb * bottom.a * (1.0 - top.a)) / max(a, 1e-5), a);
}
// A picture darkened by `amount` (0..1).
vec4 shade(vec4 c, float amount) { return vec4(c.rgb * (1.0 - amount), c.a); }

void main()
{
    float p = tve_progress;
    float k = sin(PI * p) * strength;
    float t = floor(p * 24.0);
    float row = floor(v_uv.y * 18.0);
    float shift = (hash(vec2(row, t)) - 0.5) * 0.3 * k * step(0.6, hash(vec2(row + 3.1, t)));
    vec2 uv = vec2(v_uv.x + shift, v_uv.y);
    bool showTo = p > 0.5 + (hash(vec2(floor(v_uv.x * 10.0), row + t)) - 0.5) * 0.4 * k;
    float split = 0.02 * k;
    vec4 c;
    if (showTo)
        c = vec4(toAt(uv + vec2(split, 0.0)).r, toAt(uv).g, toAt(uv - vec2(split, 0.0)).b, toAt(uv).a);
    else
        c = vec4(fromAt(uv + vec2(split, 0.0)).r, fromAt(uv).g, fromAt(uv - vec2(split, 0.0)).b, fromAt(uv).a);
    fragColor = c;
}
