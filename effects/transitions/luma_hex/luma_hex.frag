#version 440

// Hexagons: a honeycomb of hexagons opens from the middle outwards.

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

float pattern(vec2 uv)
{
    // The nearest centre of a hexagonal grid; hexagons open from the middle of the frame.
    vec2 q = uv * aspect() * 6.0;
    vec2 r = vec2(1.0, 1.7320508);
    vec2 a = mod(q, r) - r * 0.5;
    vec2 b = mod(q - r * 0.5, r) - r * 0.5;
    vec2 centre = dot(a, a) < dot(b, b) ? q - a : q - b;
    vec2 frame = centre / (aspect() * 6.0) - 0.5;
    return clamp(length(frame * aspect()) / length(0.5 * aspect()) * 0.85 + hash(centre) * 0.15, 0.0, 1.0);
}

void main()
{
    float edge = tve_progress * (1.0 + softness);
    float L = clamp(pattern(v_uv), 0.0, 1.0);
    float showFrom = smoothstep(edge - softness, edge, L);
    fragColor = mix(toAt(v_uv), fromAt(v_uv), showFrom);
}
