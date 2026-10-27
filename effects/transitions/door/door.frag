#version 440

// Door: the outgoing picture swings open like a door on its left edge

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
    float p = smoothstep(0.0, 1.0, tve_progress);
    float angle = p * 1.5707963;           // 0 to 90 degrees
    float width = cos(angle);              // projected width of the door
    float lift = sin(angle) * 0.25;        // perspective: the far edge looks taller
    vec4 behind = toAt(v_uv);
    if (v_uv.x < width && width > 0.0001) {
        float u = v_uv.x / width;
        float h = 1.0 + lift * u;
        float v = (v_uv.y - 0.5) / h + 0.5;
        vec4 door = fromAt(vec2(u, v));
        vec4 shaded = vec4(door.rgb * (1.0 - 0.5 * p * u), door.a);
        fragColor = mix(behind, shaded, door.a);
    } else {
        fragColor = behind;
    }
}
