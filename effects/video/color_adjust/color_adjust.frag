#version 440

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    float brightness;
    float contrast;
    float saturation;
    int invert;
};

layout(binding = 1) uniform sampler2D tve_source;

void main()
{
    vec4 color = texture(tve_source, v_uv);
    vec3 rgb = color.rgb + brightness;
    rgb = (rgb - 0.5) * contrast + 0.5;
    float luma = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
    rgb = mix(vec3(luma), rgb, saturation);
    if (invert != 0)
        rgb = 1.0 - rgb;
    fragColor = vec4(rgb, color.a);
}
