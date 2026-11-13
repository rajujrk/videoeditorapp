#version 440

// The beauty filter: inside the faces' skin (the mask's red, from Vision's landmarks, with the
// eyes, brows and lips cut out) and where the colour is a skin tone, fine texture smoothed by
// an edge-keeping (bilateral) blur and the skin lifted; inside the teeth area (the mask's
// green), bright pixels made whiter. skinTone() is the same as in protect.frag.

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    vec4 params; // x: smooth 0..1, y: brighten 0..1, z: teeth 0..1
};

layout(binding = 1) uniform sampler2D tve_source;
layout(binding = 2) uniform sampler2D mask; // r: skin, g: teeth (smaller; sampled smoothly)

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
    vec4 c = texture(tve_source, v_uv);
    vec2 m = texture(mask, v_uv).rg;
    float skin = m.r * skinTone(c.rgb);
    float smoothing = clamp(params.x, 0.0, 1.0) * skin;
    vec3 rgb = c.rgb;
    if (smoothing > 0.001) {
        // Two rings of 12 around the pixel, out to 0.6 % of the picture's height (two pixels at
        // least); neighbours unlike the pixel (an edge, a feature) count for little.
        vec2 radius = max(vec2(tve_texelSize.x / tve_texelSize.y, 1.0) * 0.006, tve_texelSize * 2.0);
        vec3 sum = rgb;
        float total = 1.0;
        for (int ring = 1; ring <= 2; ++ring) {
            float r = float(ring) * 0.5;
            float spatial = exp(-r * r * 1.5);
            for (int i = 0; i < 12; ++i) {
                float a = (float(i) + 0.5 * float(ring)) * 0.5235988; // 30° steps, rings offset
                vec3 s = texture(tve_source, v_uv + vec2(cos(a), sin(a)) * radius * r).rgb;
                vec3 d = s - c.rgb;
                float w = spatial * exp(-dot(d, d) / (2.0 * 0.07 * 0.07));
                sum += s * w;
                total += w;
            }
        }
        rgb = mix(rgb, sum / total, smoothing);
    }
    // Lifted as light lifts it: more in the shadows, less near white.
    rgb += clamp(params.y, 0.0, 1.0) * skin * 0.14 * (vec3(1.0) - rgb);
    // Teeth: only what is bright there (not the dark of the mouth), towards a neutral white.
    float luma = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
    float teeth = clamp(params.z, 0.0, 1.0) * m.g * smoothstep(0.25, 0.5, luma);
    rgb = mix(rgb, vec3(min(luma * 1.12 + 0.04, 1.0)), teeth * 0.75);
    fragColor = vec4(rgb, c.a);
}
