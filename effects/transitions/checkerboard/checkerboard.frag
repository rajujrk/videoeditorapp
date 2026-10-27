#version 440

// Checkerboard: squares open in two waves

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    float tve_progress;
    int squares;
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
    vec2 grid = vec2(float(squares), max(1.0, floor(float(squares) / aspect + 0.5)));
    vec2 cell = floor(v_uv * grid);
    float wave = mod(cell.x + cell.y, 2.0);                // the second wave starts half way
    float q = clamp(tve_progress * 2.0 - wave, 0.0, 1.0);
    vec2 local = abs(fract(v_uv * grid) - 0.5) * 2.0;      // 0 at the square's centre
    fragColor = max(local.x, local.y) < q ? toAt(v_uv) : fromAt(v_uv);
}
