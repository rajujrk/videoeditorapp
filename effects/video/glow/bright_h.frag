#version 440

// Glow, pass 1: the parts above the threshold, blurred horizontally.

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    float threshold;
    float radius;
    float intensity;
    vec4 tint;
};

layout(binding = 1) uniform sampler2D tve_source;

const float TVE_PI = 3.14159265359;

float tveLuma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
// Width / height of the image, and 1: multiply a uv offset by it for round shapes.
vec2 tveAspect() { return vec2(tve_texelSize.y / tve_texelSize.x, 1.0); }
float tveHeightPx() { return 1.0 / tve_texelSize.y; }
float tveHash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float tveNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(tveHash(i), tveHash(i + vec2(1.0, 0.0)), f.x),
               mix(tveHash(i + vec2(0.0, 1.0)), tveHash(i + vec2(1.0, 1.0)), f.x), f.y);
}
// The source, transparent outside its edges.
vec4 tveSample(vec2 uv)
{
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))))
        return vec4(0.0);
    return texture(tve_source, uv);
}
// Sobel gradient of the luma, `apart` pixels apart.
vec2 tveSobel(vec2 uv, float apart)
{
    vec2 d = tve_texelSize * apart;
    float tl = tveLuma(texture(tve_source, uv + vec2(-d.x, -d.y)).rgb);
    float t = tveLuma(texture(tve_source, uv + vec2(0.0, -d.y)).rgb);
    float tr = tveLuma(texture(tve_source, uv + vec2(d.x, -d.y)).rgb);
    float l = tveLuma(texture(tve_source, uv + vec2(-d.x, 0.0)).rgb);
    float r = tveLuma(texture(tve_source, uv + vec2(d.x, 0.0)).rgb);
    float bl = tveLuma(texture(tve_source, uv + vec2(-d.x, d.y)).rgb);
    float b = tveLuma(texture(tve_source, uv + vec2(0.0, d.y)).rgb);
    float br = tveLuma(texture(tve_source, uv + vec2(d.x, d.y)).rgb);
    return vec2(tr + 2.0 * r + br - tl - 2.0 * l - bl, bl + 2.0 * b + br - tl - 2.0 * t - tr);
}
// Straight-alpha `top` over `bottom`.
vec4 tveOver(vec4 top, vec4 bottom)
{
    float a = top.a + bottom.a * (1.0 - top.a);
    vec3 rgb = (top.rgb * top.a + bottom.rgb * bottom.a * (1.0 - top.a)) / max(a, 1e-5);
    return vec4(rgb, a);
}

vec4 tap(vec2 uv)
{
    vec4 s = texture(tve_source, uv);
    return vec4(max(s.rgb - threshold, 0.0) / max(1.0 - threshold, 1e-3) * s.a, 1.0);
}

void main()
{
    float blurRadius = radius / 100.0 * tveHeightPx();
    vec4 acc = vec4(0.0);
    float wsum = 0.0;
    if (blurRadius < 0.5) {
        acc = tap(v_uv);
        wsum = 1.0;
    } else {
        float sigma = blurRadius / 2.0;
        float stride = max(1.0, blurRadius / 16.0);
        for (int i = -16; i <= 16; ++i) {
            float x = float(i) * stride;
            if (abs(x) > blurRadius)
                continue;
            float w = exp(-0.5 * x * x / (sigma * sigma));
            acc += w * tap(v_uv + vec2(tve_texelSize.x, 0.0) * x);
            wsum += w;
        }
    }
    vec4 blurred = acc / wsum;
    fragColor = blurred;
}
