#version 440

// Protecting skin tones: where the picture before its look and grade was skin, the graded
// picture is mixed back towards it by the amount. skinTone() is the same as in beauty.frag and
// tve::skinTone (core/model/ColorGrade.cpp).

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    vec4 params; // x: amount 0..1
};

layout(binding = 1) uniform sampler2D tve_source; // graded
layout(binding = 2) uniform sampler2D original;   // before the look and grade

vec3 rgbToHsv(vec3 c)
{
    float maxC = max(c.r, max(c.g, c.b));
    float minC = min(c.r, min(c.g, c.b));
    float d = maxC - minC;
    float h = 0.0;
    if (d > 1e-6) {
        if (maxC == c.r)
            h = mod((c.g - c.b) / d, 6.0);
        else if (maxC == c.g)
            h = (c.b - c.r) / d + 2.0;
        else
            h = (c.r - c.g) / d + 4.0;
        h /= 6.0;
    }
    return vec3(h, maxC > 1e-6 ? d / maxC : 0.0, maxC);
}

float skinTone(vec3 rgb)
{
    vec3 hsv = rgbToHsv(clamp(rgb, 0.0, 1.0));
    float hue = hsv.x > 0.5 ? hsv.x - 1.0 : hsv.x;
    float hueWeight = smoothstep(-0.06, -0.01, hue) * (1.0 - smoothstep(0.12, 0.17, hue));
    float satWeight = smoothstep(0.08, 0.16, hsv.y) * (1.0 - smoothstep(0.72, 0.92, hsv.y));
    float valueWeight = smoothstep(0.12, 0.22, hsv.z) * (1.0 - smoothstep(0.97, 1.0, hsv.z));
    return hueWeight * satWeight * valueWeight;
}

void main()
{
    vec4 graded = texture(tve_source, v_uv);
    vec4 before = texture(original, v_uv);
    float keep = clamp(params.x, 0.0, 1.0) * skinTone(before.rgb);
    fragColor = vec4(mix(graded.rgb, before.rgb, keep), graded.a);
}
