#version 440

// Horizontal half of a separable Gaussian blur. Up to 16 texels each way the taps are read
// in pairs (17 reads); wider radii sample with a stride so the cost stays at most 33 taps per pass.

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
    if (stride <= 1.0) {
        // Neighbouring taps in pairs: one linear fetch between texels i and i + 1, placed by
        // their weights, gives both — the same blur from about half the reads.
        sum = texture(tve_source, v_uv);
        weightSum = 1.0;
        for (int i = 1; i <= kHalfTaps; i += 2) {
            float a = float(i);
            float b = a + 1.0;
            float wa = a > radius ? 0.0 : exp(-0.5 * a * a / (sigma * sigma));
            float wb = b > radius ? 0.0 : exp(-0.5 * b * b / (sigma * sigma));
            float w = wa + wb;
            if (w <= 0.0)
                continue;
            float x = (a * wa + b * wb) / w;
            sum += w * (texture(tve_source, v_uv + vec2(x * tve_texelSize.x, 0.0)) + texture(tve_source, v_uv - vec2(x * tve_texelSize.x, 0.0)));
            weightSum += 2.0 * w;
        }
    } else {
        for (int i = -kHalfTaps; i <= kHalfTaps; ++i) {
            float x = float(i) * stride;
            if (abs(x) > radius)
                continue;
            float w = exp(-0.5 * x * x / (sigma * sigma));
            sum += w * texture(tve_source, v_uv + vec2(x * tve_texelSize.x, 0.0));
            weightSum += w;
        }
    }
    fragColor = sum / weightSum;
}
