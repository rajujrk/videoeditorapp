#version 440

// Star: a star grows from the centre, the incoming picture inside it.

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

float shapeDistance(vec2 q)
{
    // A five-pointed star of radius 0.5.
    const vec2 k1 = vec2(0.809016994, -0.587785252);
    const vec2 k2 = vec2(-k1.x, k1.y);
    q.x = abs(q.x);
    q -= 2.0 * max(dot(k1, q), 0.0) * k1;
    q -= 2.0 * max(dot(k2, q), 0.0) * k2;
    q.x = abs(q.x);
    q.y -= 0.5;
    vec2 ba = 0.45 * vec2(-k1.y, k1.x) - vec2(0.0, 1.0);
    float h = clamp(dot(q, ba) / dot(ba, ba), 0.0, 0.5);
    return length(q - ba * h) * sign(q.y * ba.x - q.x * ba.y);
}

void main()
{
    float p = tve_progress;
    if (p <= 0.0) {
        fragColor = fromAt(v_uv);
        return;
    }
    vec2 q = (v_uv - 0.5) * aspect() / (p * 5.0 + 1e-4);
    float d = shapeDistance(vec2(q.x, -q.y)); // y up
    float inside = 1.0 - smoothstep(-softness * 0.2, 0.0, d);
    fragColor = mix(fromAt(v_uv), toAt(v_uv), p >= 1.0 ? 1.0 : inside);
}
