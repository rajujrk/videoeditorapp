#version 440

// Temporal denoise: the frame mixed with the denoised frame before it where they agree (still
// parts: grain averages out over frames), kept as it is where they differ (motion: no
// ghosts). Neighbours are compared, not single pixels, so grain itself does not count as
// motion.

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    vec4 params; // x: strength 0..1
};

layout(binding = 1) uniform sampler2D tve_source;
layout(binding = 2) uniform sampler2D previous;

void main()
{
    vec4 now = texture(tve_source, v_uv);
    vec4 before = texture(previous, v_uv);
    // Difference of 3 × 3 means: grain cancels, motion does not.
    vec3 a = vec3(0.0);
    vec3 b = vec3(0.0);
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            vec2 o = vec2(float(x), float(y)) * tve_texelSize;
            a += texture(tve_source, v_uv + o).rgb;
            b += texture(previous, v_uv + o).rgb;
        }
    float difference = length(a - b) / 9.0;
    float threshold = mix(0.01, 0.06, params.x);
    float still = 1.0 - smoothstep(threshold, threshold * 2.5, difference);
    float keep = mix(0.0, 0.85, params.x) * still; // how much of the frame before
    fragColor = vec4(mix(now.rgb, before.rgb, keep), now.a);
}
