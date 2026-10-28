#version 440

// Tiles: the outgoing picture breaks into tiles that turn and shrink away, one after another.

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    float tve_progress;
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
    vec2 grid = vec2(8.0, 5.0);
    vec2 cell = floor(v_uv * grid);
    float delay = hash(cell) * 0.5;
    float t = clamp((p - delay) / 0.5, 0.0, 1.0);
    float s = 1.0 - smoothstep(0.0, 1.0, t);
    vec2 centre = (cell + 0.5) / grid;
    vec2 q = (v_uv - centre) * grid * aspect();
    float a = t * 1.5;
    q = vec2(cos(a) * q.x + sin(a) * q.y, -sin(a) * q.x + cos(a) * q.y) / max(s, 1e-4);
    vec2 local = q / aspect();
    vec4 tile = s > 0.0 && abs(local.x) <= 0.5 && abs(local.y) <= 0.5 ? fromAt(centre + local / grid) : vec4(0.0);
    fragColor = over(tile, toAt(v_uv));
}
