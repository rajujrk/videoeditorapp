#version 440

// Vertical half of the separable Gaussian blur; see blur_h.frag.

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    float radius;
};

layout(binding = 1) uniform sampler2D tve_source;

const int kHalfTaps = 16;

void main()
{
    if (radius < 0.5) {
        fragColor = texture(tve_source, v_uv);
        return;
    }
    float sigma = radius / 2.0;
    float stride = max(1.0, radius / float(kHalfTaps));
    vec4 sum = vec4(0.0);
    float weightSum = 0.0;
    for (int i = -kHalfTaps; i <= kHalfTaps; ++i) {
        float y = float(i) * stride;
        if (abs(y) > radius)
            continue;
        float w = exp(-0.5 * y * y / (sigma * sigma));
        sum += w * texture(tve_source, v_uv + vec2(0.0, y * tve_texelSize.y));
        weightSum += w;
    }
    fragColor = sum / weightSum;
}
