#version 440

// Colour management (model/Colour.h, which this mirrors): a picture from one colour space
// to another — its transfer decoded to relative light (1 = reference white, 203 nits), the
// linear primaries matrix, gamut clipped, highlights rolled off by the brightest channel
// towards a peak, then the destination's transfer. Transfers: 0 SDR (a 2.4 power), 1 linear,
// 2 PQ, 3 HLG (as on a 1000-nit display, system gamma 1.2).

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    vec4 row0;   // the matrix's rows (xyz)
    vec4 row1;
    vec4 row2;
    vec4 params; // x: from, y: to, z: target peak (0: none)
};

layout(binding = 1) uniform sampler2D tve_source;

const float kReferenceWhite = 203.0;
const float kPeak = 1000.0;
const float m1 = 2610.0 / 16384.0, m2 = 2523.0 / 4096.0 * 128.0;
const float c1 = 3424.0 / 4096.0, c2 = 2413.0 / 4096.0 * 32.0, c3 = 2392.0 / 4096.0 * 32.0;
const float ha = 0.17883277, hb = 0.28466892, hc = 0.55991073;
const vec3 kLuma2020 = vec3(0.2627, 0.6780, 0.0593);

vec3 pqDecode(vec3 v)
{
    vec3 p = pow(clamp(v, 0.0, 1.0), vec3(1.0 / m2));
    return 10000.0 * pow(max(p - c1, 0.0) / (c2 - c3 * p), vec3(1.0 / m1)) / kReferenceWhite;
}
vec3 pqEncode(vec3 light)
{
    vec3 y = pow(clamp(light * kReferenceWhite / 10000.0, 0.0, 1.0), vec3(m1));
    return pow((c1 + c2 * y) / (1.0 + c3 * y), vec3(m2));
}
float hlgInverse(float v)
{
    v = clamp(v, 0.0, 1.0);
    return v <= 0.5 ? v * v / 3.0 : (exp((v - hc) / ha) + hb) / 12.0;
}
float hlgForward(float e)
{
    e = max(e, 0.0);
    return e <= 1.0 / 12.0 ? sqrt(3.0 * e) : ha * log(12.0 * e - hb) + hc;
}
vec3 hlgDecode(vec3 v)
{
    vec3 scene = vec3(hlgInverse(v.r), hlgInverse(v.g), hlgInverse(v.b));
    float gain = pow(max(dot(scene, kLuma2020), 1e-9), 0.2) * kPeak / kReferenceWhite;
    return scene * gain;
}
vec3 hlgEncode(vec3 light)
{
    vec3 display = light * kReferenceWhite / kPeak;
    float gain = pow(max(dot(display, kLuma2020), 1e-9), (1.0 - 1.2) / 1.2);
    vec3 e = display * gain;
    return vec3(hlgForward(e.r), hlgForward(e.g), hlgForward(e.b));
}

vec3 decodeTransfer(vec3 v, int t)
{
    if (t == 0)
        return pow(clamp(v, 0.0, 1.0), vec3(2.4));
    if (t == 2)
        return pqDecode(v);
    if (t == 3)
        return hlgDecode(v);
    return v;
}
vec3 encodeTransfer(vec3 light, int t)
{
    if (t == 0)
        return pow(clamp(light, 0.0, 1.0), vec3(1.0 / 2.4));
    if (t == 2)
        return pqEncode(light);
    if (t == 3)
        return hlgEncode(light);
    return light;
}

float rollOff(float x, float peak)
{
    float knee = 0.75 * peak;
    if (x <= knee)
        return x;
    float room = peak - knee;
    return knee + room * (1.0 - exp(-(x - knee) / room));
}

void main()
{
    vec4 c = texture(tve_source, v_uv);
    vec3 light = decodeTransfer(c.rgb, int(params.x));
    vec3 rgb = max(vec3(dot(row0.xyz, light), dot(row1.xyz, light), dot(row2.xyz, light)), 0.0);
    if (params.z > 0.0) {
        float brightest = max(rgb.r, max(rgb.g, rgb.b));
        if (brightest > 1e-6)
            rgb *= rollOff(brightest, params.z) / brightest;
    }
    fragColor = vec4(encodeTransfer(rgb, int(params.y)), c.a);
}
