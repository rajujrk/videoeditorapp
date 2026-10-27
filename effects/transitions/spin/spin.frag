#version 440

// Spin: the outgoing picture spins away and the incoming one spins in

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    float tve_progress;
    float turns;
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
    bool outgoing = p < 0.5;
    float q = outgoing ? p * 2.0 : (1.0 - p) * 2.0; // 0 at rest, 1 at the cut
    float angle = (outgoing ? 1.0 : -1.0) * q * turns * 6.28318530718;
    float scale = max(1.0 - q, 0.0001);
    // Undo the rotation and scale about the centre, in square pixels.
    vec2 c = (v_uv - 0.5) * vec2(aspect, 1.0);
    vec2 r = vec2(cos(angle) * c.x + sin(angle) * c.y, -sin(angle) * c.x + cos(angle) * c.y) / scale;
    vec2 uv = r / vec2(aspect, 1.0) + 0.5;
    fragColor = outgoing ? fromAt(uv) : toAt(uv);
}
