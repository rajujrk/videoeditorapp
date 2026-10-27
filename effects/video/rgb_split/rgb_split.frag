#version 440

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    float amount; // offset in source pixels
    float angle;  // degrees
    int channels; // 0 red-blue, 1 red-green, 2 green-blue
};

layout(binding = 1) uniform sampler2D tve_source;

void main()
{
    float a = radians(angle);
    vec2 offset = vec2(cos(a), sin(a)) * amount * tve_texelSize;
    vec4 center = texture(tve_source, v_uv);
    vec4 ahead = texture(tve_source, v_uv + offset);
    vec4 behind = texture(tve_source, v_uv - offset);

    vec3 rgb = center.rgb;
    if (channels == 0) {
        rgb.r = ahead.r;
        rgb.b = behind.b;
    } else if (channels == 1) {
        rgb.r = ahead.r;
        rgb.g = behind.g;
    } else {
        rgb.g = ahead.g;
        rgb.b = behind.b;
    }
    fragColor = vec4(rgb, center.a);
}
