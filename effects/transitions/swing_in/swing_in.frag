#version 440

// Swing in: the incoming picture swings shut over the outgoing one like a door on its hinge.

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
    float p = smoothstep(0.0, 1.0, tve_progress);
    vec2 f = uvToFrame(v_uv);
    float w = sin(p * PI / 2.0);          // projected width of the door
    float lift = cos(p * PI / 2.0) * 0.3; // the far edge looks taller while it is open
    vec4 behind = fromAt(v_uv);
    if (f.x < w) {
        float u = f.x / max(w, 1e-5);
        float h = 1.0 + lift * u;
        vec4 door = toAt(frameToUv(vec2(u, (f.y - 0.5) / h + 0.5)));
        fragColor = over(shade(door, 0.5 * (1.0 - p) * u), behind);
    } else {
        fragColor = behind;
    }
}
