#version 440

// Night view: a dark shot lifted as a camera's night mode would. First the grain the lift
// would bring up is smoothed (an edge-keeping blur over two rings: the brightness gently, the
// colour noise more), then the brightness is raised along a curve that lifts the shadows a
// lot and the highlights hardly, the colours scaled with it so they keep their hue.

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    vec4 params; // x: strength 0..1, y: denoise 0..1
};

layout(binding = 1) uniform sampler2D tve_source;

const vec3 kLuma = vec3(0.2126, 0.7152, 0.0722);

void main()
{
    vec4 c = texture(tve_source, v_uv);
    float strength = clamp(params.x, 0.0, 1.0);
    float denoise = clamp(params.y, 0.0, 1.0);
    vec3 rgb = c.rgb;

    if (denoise > 0.001) {
        // Two rings of 12, out to 0.4 % of the height (two pixels at least).
        vec2 radius = max(vec2(tve_texelSize.x / tve_texelSize.y, 1.0) * 0.004, tve_texelSize * 2.0);
        float y0 = dot(c.rgb, kLuma);
        // Dark grain is small in value: the range is narrower than for the beauty filter.
        float sigma = mix(0.02, 0.08, denoise);
        float lumaSum = y0, lumaTotal = 1.0;
        vec3 chromaSum = c.rgb - vec3(y0);
        float chromaTotal = 1.0;
        for (int ring = 1; ring <= 2; ++ring) {
            float r = float(ring) * 0.5;
            float spatial = exp(-r * r);
            for (int i = 0; i < 12; ++i) {
                float a = (float(i) + 0.5 * float(ring)) * 0.5235988;
                vec3 s = texture(tve_source, v_uv + vec2(cos(a), sin(a)) * radius * r).rgb;
                float y = dot(s, kLuma);
                float d = y - y0;
                float w = spatial * exp(-d * d / (2.0 * sigma * sigma));
                lumaSum += y * w;
                lumaTotal += w;
                // Colour noise: averaged more freely (only big brightness changes keep it apart).
                float wc = spatial * exp(-d * d / (2.0 * 0.15 * 0.15));
                chromaSum += (s - vec3(y)) * wc;
                chromaTotal += wc;
            }
        }
        float y = mix(y0, lumaSum / lumaTotal, denoise * 0.8);
        vec3 chroma = mix(c.rgb - vec3(y0), chromaSum / chromaTotal, denoise);
        rgb = vec3(y) + chroma;
    }

    if (strength > 0.001) {
        // y' = (1 - e^(-k y)) / (1 - e^(-k)): steep from black (a finite slope there, so no
        // step between black and nearly black), flattening towards white. The colours are
        // scaled with the brightness; where that would clip, they are drawn towards the
        // lifted grey instead (hue kept, nothing burnt out).
        float y = max(dot(rgb, kLuma), 0.0);
        float k = mix(0.25, 6.0, strength);
        float lifted = (1.0 - exp(-k * y)) / (1.0 - exp(-k));
        float gain = y > 1e-4 ? lifted / y : k / (1.0 - exp(-k));
        vec3 scaled = rgb * gain;
        float most = max(scaled.r, max(scaled.g, scaled.b));
        if (most > 1.0)
            scaled = vec3(lifted) + (scaled - vec3(lifted)) * ((1.0 - lifted) / max(most - lifted, 1e-4));
        rgb = scaled;
    }
    fragColor = vec4(clamp(rgb, 0.0, 1.0), c.a);
}
