#version 440

// Page turn: the outgoing picture is a page folded back from one edge, its pale back showing,
// uncovering the incoming one.

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    float tve_progress;
    int direction;
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

vec2 uvToFrame(vec2 uv)
{
    if (direction == 1) return vec2(1.0 - uv.x, uv.y);
    if (direction == 2) return vec2(uv.y, uv.x);
    if (direction == 3) return vec2(1.0 - uv.y, uv.x);
    return uv;
}
vec2 frameToUv(vec2 f)
{
    if (direction == 1) return vec2(1.0 - f.x, f.y);
    if (direction == 2) return vec2(f.y, f.x);
    if (direction == 3) return vec2(f.y, 1.0 - f.x);
    return f;
}

void main()
{
    float p = tve_progress;
    vec2 f = uvToFrame(v_uv);
    float fold = 1.0 - p * 1.0; // the fold line moves from the right edge to the left
    vec4 under = toAt(v_uv);
    vec4 page = f.x < fold ? fromAt(v_uv) : vec4(0.0);
    // The turned part of the page lies mirrored to the left of the fold.
    float back = 2.0 * fold - f.x;
    if (f.x < fold && back <= 1.0 && back >= fold) {
        vec4 c = fromAt(frameToUv(vec2(back, f.y)));
        vec4 paper = vec4(mix(c.rgb, vec3(0.92), 0.7) * (0.85 + 0.15 * (fold - f.x) / max(1.0 - fold, 1e-5)), c.a);
        page = paper;
    }
    // A soft shadow cast on the uncovered picture by the fold.
    float shadow = f.x >= fold ? 0.4 * (1.0 - smoothstep(0.0, 0.08, f.x - fold)) * sin(PI * p) : 0.0;
    fragColor = over(page, shade(under, shadow));
}
