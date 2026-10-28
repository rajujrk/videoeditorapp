#version 440

// Wraps the picture around a point: the bottom edge becomes a small round world, the top
// its sky (a polar mapping).

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    float zoom;
    float rotation;
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

void main()
{
    vec2 aspect = tveAspect();
    vec2 p = (v_uv - 0.5) * aspect / zoom;
    float r = length(p) * 2.0;
    float a = atan(p.x, -p.y) + radians(rotation);
    vec2 uv = vec2(fract(a / (2.0 * TVE_PI) + 0.5), clamp(1.0 - r, 0.0, 1.0));
    fragColor = texture(tve_source, uv);
}
