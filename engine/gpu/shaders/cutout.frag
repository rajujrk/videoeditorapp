#version 440

// Portrait cutout: the picture kept where the person matte (from Vision, smaller than the
// picture, sampled smoothly) says there is a person — or, inverted, everywhere else. The
// edge is a soft step across the matte's 0.5 level, moved out or in by the shift. With a blur,
// what is cut away is blurred instead of made transparent (a background blur).

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    vec4 params; // x: softness 0..1, y: shift -1..1, z: invert (0/1), w: blur 0..1 (0: transparent)
};

layout(binding = 1) uniform sampler2D tve_source;
layout(binding = 2) uniform sampler2D matte;

void main()
{
    vec4 c = texture(tve_source, v_uv);
    float m = texture(matte, v_uv).r;
    float edge = 0.5 - 0.4 * params.y;
    float width = mix(0.01, 0.45, params.x);
    float kept = smoothstep(edge - width, edge + width, m);
    if (params.z > 0.5)
        kept = 1.0 - kept;
    if (params.w <= 0.0) {
        fragColor = vec4(c.rgb, c.a * kept);
        return;
    }
    // A disc of 48 samples (three rings of 16, turned against each other), out to up to 4 %
    // of the picture's height, weighted only by distance: a lens-like background blur.
    vec4 blurred = c;
    float total = 1.0;
    if (kept < 0.999) {
        vec2 radius = vec2(tve_texelSize.x / tve_texelSize.y, 1.0) * mix(0.004, 0.04, params.w);
        for (int ring = 1; ring <= 3; ++ring) {
            float r = float(ring) / 3.0;
            for (int i = 0; i < 16; ++i) {
                float a = (float(i) + 0.33 * float(ring)) * 0.3926991; // 22.5° steps
                blurred += texture(tve_source, v_uv + vec2(cos(a), sin(a)) * radius * r);
                total += 1.0;
            }
        }
        blurred /= total;
    }
    fragColor = mix(blurred, c, kept);
}
