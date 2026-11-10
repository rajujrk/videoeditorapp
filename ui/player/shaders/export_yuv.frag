#version 440

// Export: one plane of NV12 from the finished RGB picture, BT.709 video range (what the
// encoder is told, and what playback decodes back). Plane 0 writes luma to a full-size R8
// target; plane 1 writes CbCr to a half-size RG8 target, where the bilinear sample at each
// texel centre averages the 2 × 2 pixels it covers. Plane 2 writes the picture itself to an
// RGBA8 target (ProRes and GIF take RGB). With `hdr`, the planes are P010 instead: BT.2020
// non-constant luminance, 10-bit video-range codes in the high bits of R16 / RG16 targets
// (the picture is already PQ or HLG).

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    int plane;
    int hdr;
};

layout(binding = 1) uniform sampler2D tve_source;

void main()
{
    vec3 rgb = clamp(texture(tve_source, v_uv).rgb, 0.0, 1.0);
    if (hdr != 0) {
        float y = dot(rgb, vec3(0.2627, 0.6780, 0.0593));
        // A 10-bit code c, stored as c × 64 of 65535.
        if (plane == 0) {
            fragColor = vec4(round(64.0 + 876.0 * y) * 64.0 / 65535.0, 0.0, 0.0, 1.0);
        } else {
            float cb = (rgb.b - y) / 1.8814;
            float cr = (rgb.r - y) / 1.4746;
            fragColor = vec4(round(512.0 + 896.0 * cb) * 64.0 / 65535.0, round(512.0 + 896.0 * cr) * 64.0 / 65535.0,
                             0.0, 1.0);
        }
        return;
    }
    float y = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
    if (plane == 2) {
        fragColor = vec4(rgb, 1.0);
    } else if (plane == 0) {
        fragColor = vec4((16.0 + 219.0 * y) / 255.0, 0.0, 0.0, 1.0);
    } else {
        float cb = (rgb.b - y) / 1.8556;
        float cr = (rgb.r - y) / 1.5748;
        fragColor = vec4((128.0 + 224.0 * cb) / 255.0, (128.0 + 224.0 * cr) / 255.0, 0.0, 1.0);
    }
}
