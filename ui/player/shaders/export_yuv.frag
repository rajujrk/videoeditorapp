#version 440

// Export: one plane of NV12 from the finished RGB picture, BT.709 video range (what the
// encoder is told, and what playback decodes back). Plane 0 writes luma to a full-size R8
// target; plane 1 writes CbCr to a half-size RG8 target, where the bilinear sample at each
// texel centre averages the 2 × 2 pixels it covers.

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    int plane;
};

layout(binding = 1) uniform sampler2D tve_source;

void main()
{
    vec3 rgb = clamp(texture(tve_source, v_uv).rgb, 0.0, 1.0);
    float y = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
    if (plane == 0) {
        fragColor = vec4((16.0 + 219.0 * y) / 255.0, 0.0, 0.0, 1.0);
    } else {
        float cb = (rgb.b - y) / 1.8556;
        float cr = (rgb.r - y) / 1.5748;
        fragColor = vec4((128.0 + 224.0 * cb) / 255.0, (128.0 + 224.0 * cr) / 255.0, 0.0, 1.0);
    }
}
