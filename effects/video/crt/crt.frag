#version 440

// An old tube screen: bulging glass, scan lines, a phosphor mask, darker corners.

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    float curvature;
    float scanlines;
    float mask;
    float vignette;
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
    vec2 uv = v_uv * 2.0 - 1.0;
    uv *= 1.0 + curvature * dot(uv, uv) * 0.25;
    if (abs(uv.x) > 1.0 || abs(uv.y) > 1.0) {
        fragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    vec2 suv = uv * 0.5 + 0.5;
    vec4 c = texture(tve_source, suv);
    vec3 rgb = c.rgb * (1.0 - scanlines * 0.5 * (0.5 - 0.5 * cos(suv.y * 270.0 * 2.0 * TVE_PI)));
    float column = mod(floor(suv.x / tve_texelSize.x / max(1.0, tveHeightPx() / 1080.0)), 3.0);
    vec3 phosphor = column < 1.0 ? vec3(1.2, 0.7, 0.7) : column < 2.0 ? vec3(0.7, 1.2, 0.7) : vec3(0.7, 0.7, 1.2);
    rgb *= mix(vec3(1.0), phosphor, mask);
    rgb *= mix(1.0, smoothstep(1.6, 0.5, length(uv)), vignette);
    fragColor = vec4(clamp(rgb, 0.0, 1.0), c.a);
}
