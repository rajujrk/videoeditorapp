#version 440

// A clip's colour grade (see ColorGrade.h and applyGrade(), which this mirrors): exposure
// and white balance in linear light, contrast, lift/gain then gamma, offset, saturation,
// tone curves from a lookup texture, then a 3D LUT mixed in by its intensity.

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    vec4 linearGain; // xyz
    vec4 lift;       // xyz; w: contrast
    vec4 gammaExp;   // xyz; w: saturation
    vec4 gain;       // xyz; w: 1 when the curves apply
    vec4 offset;     // xyz; w: LUT intensity (0: none)
    vec4 lutMin;     // xyz: domain start; w: LUT size
    vec4 lutScale;   // xyz: 1 / domain size
};

layout(binding = 1) uniform sampler2D tve_source;
layout(binding = 2) uniform sampler2D curves; // 256 × 1: r, g, b curves
layout(binding = 3) uniform sampler3D lut;

const float kMidGrey = 0.435;

float curve(float v, int channel)
{
    vec4 sampled = texture(curves, vec2((v * 255.0 + 0.5) / 256.0, 0.5));
    return channel == 0 ? sampled.r : channel == 1 ? sampled.g : sampled.b;
}

void main()
{
    vec4 color = texture(tve_source, v_uv);
    vec3 c = pow(max(color.rgb, 0.0), vec3(2.2)) * linearGain.xyz;
    c = pow(c, vec3(1.0 / 2.2));
    c = (c - kMidGrey) * lift.w + kMidGrey;
    c = gain.xyz * (c + lift.xyz * (1.0 - c));
    c = pow(max(c, 0.0), gammaExp.xyz);
    c += offset.xyz;
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = clamp(mix(vec3(luma), c, gammaExp.w), 0.0, 1.0);
    if (gain.w > 0.5)
        c = vec3(curve(c.r, 0), curve(c.g, 1), curve(c.b, 2));
    if (offset.w > 0.0) {
        float n = lutMin.w;
        vec3 u = clamp((c - lutMin.xyz) * lutScale.xyz, 0.0, 1.0);
        vec3 graded = texture(lut, (u * (n - 1.0) + 0.5) / n).rgb;
        c = mix(c, graded, offset.w);
    }
    fragColor = vec4(c, color.a);
}
