#version 440

// A clip's colour grade (see ColorGrade.h and applyGrade(), which this mirrors): exposure
// and white balance in linear light, contrast, lift/gain then gamma, offset, saturation,
// HSL bands and hue curves, tone curves from a lookup texture, then a 3D LUT mixed in by
// its intensity.

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
    vec4 hslBands[8]; // per band: hue shift (turns), saturation factor, value factor, centre (degrees)
    vec4 flags;       // x: HSL bands apply; y: hue curves apply
};

layout(binding = 1) uniform sampler2D tve_source;
layout(binding = 2) uniform sampler2D curves; // 256 × 2: tone curves (r, g, b); hue curves
layout(binding = 3) uniform sampler3D lut;

const float kMidGrey = 0.435;

vec4 curveRow(float v, float row)
{
    return texture(curves, vec2((clamp(v, 0.0, 1.0) * 255.0 + 0.5) / 256.0, row));
}

float curve(float v, int channel)
{
    vec4 sampled = curveRow(v, 0.25);
    return channel == 0 ? sampled.r : channel == 1 ? sampled.g : sampled.b;
}

vec3 rgbToHsv(vec3 c)
{
    float maxC = max(c.r, max(c.g, c.b));
    float minC = min(c.r, min(c.g, c.b));
    float delta = maxC - minC;
    float hue = 0.0;
    if (delta > 1e-6) {
        if (maxC == c.r)
            hue = mod((c.g - c.b) / delta + 6.0, 6.0);
        else if (maxC == c.g)
            hue = (c.b - c.r) / delta + 2.0;
        else
            hue = (c.r - c.g) / delta + 4.0;
        hue /= 6.0;
    }
    return vec3(hue, maxC > 1e-6 ? delta / maxC : 0.0, maxC);
}

vec3 hsvToRgb(vec3 hsv)
{
    float h = fract(hsv.x) * 6.0;
    vec3 k = mod(vec3(5.0, 3.0, 1.0) + h, 6.0);
    return hsv.z - hsv.z * hsv.y * clamp(min(k, 4.0 - k), 0.0, 1.0);
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
    if (flags.x > 0.5 || flags.y > 0.5) {
        vec3 hsv = rgbToHsv(c);
        float hue = hsv.x;
        if (flags.x > 0.5) {
            // The two bands around this hue, blended by where it falls between their centres.
            float degrees = hue * 360.0;
            int a = 7;
            for (int b = 0; b < 8; ++b) {
                if (degrees >= hslBands[b].w)
                    a = b;
            }
            int next = (a + 1) % 8;
            float to = next == 0 ? 360.0 : hslBands[next].w;
            float t = (degrees - hslBands[a].w) / (to - hslBands[a].w);
            vec3 adjust = mix(hslBands[a].xyz, hslBands[next].xyz, t);
            float strength = clamp(hsv.y / 0.15, 0.0, 1.0); // greys stay grey
            hsv.x += adjust.x * strength;
            hsv.y *= 1.0 + (adjust.y - 1.0) * strength;
            hsv.z *= 1.0 + (adjust.z - 1.0) * strength;
        }
        if (flags.y > 0.5) {
            vec4 byHue = curveRow(hue, 0.75);
            hsv.y *= 2.0 * byHue.r;
            hsv.x += (byHue.g - 0.5) / 3.0; // ±60°
            hsv.y *= 2.0 * curveRow(clamp(luma, 0.0, 1.0), 0.75).b;
        }
        c = hsvToRgb(vec3(hsv.x, clamp(hsv.y, 0.0, 1.0), clamp(hsv.z, 0.0, 1.0)));
    }
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
