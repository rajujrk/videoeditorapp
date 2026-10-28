#version 440

// Whip pan: a fast camera pan, the pictures smeared by motion blur as they pass.

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

vec4 at(vec2 f, float p)
{
    // The outgoing picture moves off to the left, the incoming follows it in.
    vec2 a = vec2(f.x + p, f.y);
    return a.x < 1.0 ? fromAt(frameToUv(a)) : toAt(frameToUv(vec2(a.x - 1.0, f.y)));
}

void main()
{
    float p = smoothstep(0.0, 1.0, tve_progress);
    float blur = sin(PI * tve_progress) * 0.15;
    vec2 f = uvToFrame(v_uv);
    vec4 sum = vec4(0.0);
    for (int i = 0; i < 16; ++i)
        sum += at(vec2(f.x + (float(i) / 15.0 - 0.5) * blur, f.y), p);
    fragColor = blur > 0.0 ? sum / 16.0 : at(f, p);
}
