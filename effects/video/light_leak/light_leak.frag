#version 440

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    float amount;
    float hue;
    float speed;
};

layout(binding = 1) uniform sampler2D tve_source;

float hash(vec2 p)
{
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

// Warm light washing in from the edges: soft blobs drifting over time, laid over the
// picture with a screen blend.
vec3 hueColor(float h)
{
    return clamp(abs(mod(h * 6.0 + vec3(0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0, 0.0, 1.0);
}

void main()
{
    vec4 source = texture(tve_source, v_uv);
    float t = tve_time * speed;
    float aspect = tve_texelSize.y / tve_texelSize.x;
    vec2 p = vec2(v_uv.x * aspect, v_uv.y);
    vec2 a = vec2(-0.1 + 0.15 * sin(t * 0.7), 0.2 + 0.25 * sin(t * 0.43));
    vec2 b = vec2(aspect * (1.05 + 0.1 * cos(t * 0.5)), 0.8 + 0.2 * cos(t * 0.61));
    float glow = exp(-dot(p - a, p - a) * 4.0) + 0.8 * exp(-dot(p - b, p - b) * 5.0);
    vec3 light = mix(hueColor(hue), vec3(1.0, 0.95, 0.85), 0.25) * clamp(glow, 0.0, 1.0) * amount;
    vec3 screened = 1.0 - (1.0 - source.rgb) * (1.0 - light);
    fragColor = vec4(screened, source.a);
}
