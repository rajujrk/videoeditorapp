#version 440

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    float opacity;
    vec4 color;
};

layout(binding = 1) uniform sampler2D tve_source;

float hash(vec2 p)
{
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

// A camera viewfinder over the picture: corner brackets, a centre cross, a thirds grid, a
// blinking record light and a battery gauge.
float box(vec2 p, vec2 lo, vec2 hi)
{
    return step(lo.x, p.x) * step(p.x, hi.x) * step(lo.y, p.y) * step(p.y, hi.y);
}

void main()
{
    vec4 source = texture(tve_source, v_uv);
    vec2 px = v_uv / tve_texelSize;            // pixels from the top left
    vec2 size = 1.0 / tve_texelSize;
    float unit = size.y / 270.0;               // scales with the picture
    float t = 2.0 * unit;                      // line thickness
    vec2 m = vec2(24.0 * unit);                // margin
    float arm = 40.0 * unit;
    vec2 p = px;
    vec2 q = size - px;
    float mark = 0.0;
    // Corner brackets
    for (int corner = 0; corner < 4; ++corner) {
        vec2 c = vec2(corner == 1 || corner == 3 ? q.x : p.x, corner >= 2 ? q.y : p.y) - m;
        mark = max(mark, box(c, vec2(0.0), vec2(arm, t)) + box(c, vec2(0.0), vec2(t, arm)));
    }
    // Centre cross
    vec2 d = abs(px - size * 0.5);
    mark = max(mark, box(d, vec2(0.0), vec2(14.0 * unit, t * 0.5)) + box(d, vec2(0.0), vec2(t * 0.5, 14.0 * unit)));
    // Thirds, faint
    vec2 third = abs(fract(v_uv * 3.0 + 0.5) - 0.5) * size / 3.0;
    float grid = (step(third.x, t * 0.25) + step(third.y, t * 0.25)) * 0.25;
    // Record light, blinking once a second (top left, inside the bracket), and a battery
    // (top right)
    float rec = step(length(px - vec2(m.x + 18.0 * unit, m.y + 18.0 * unit)), 7.0 * unit)
              * step(0.5, fract(tve_time + 0.5));
    vec2 b = px - vec2(size.x - m.x - 50.0 * unit, m.y + 10.0 * unit);
    float battery = box(b, vec2(0.0), vec2(36.0 * unit, 16.0 * unit)) - box(b, vec2(t), vec2(36.0 * unit - t, 16.0 * unit - t));
    battery += box(b, vec2(t * 2.0), vec2(24.0 * unit, 16.0 * unit - t * 2.0)) + box(b, vec2(36.0 * unit, 5.0 * unit), vec2(40.0 * unit, 11.0 * unit));
    vec3 c = mix(source.rgb, color.rgb, clamp(max(mark, max(grid, battery)), 0.0, 1.0) * opacity * color.a);
    c = mix(c, vec3(0.95, 0.15, 0.15), rec * opacity);
    fragColor = vec4(c, source.a);
}
