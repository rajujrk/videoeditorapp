#version 440

// Broken on purpose: a transition without tve_progress.

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
};

layout(binding = 1) uniform sampler2D tve_from;
layout(binding = 2) uniform sampler2D tve_to;

void main()
{
    fragColor = mix(texture(tve_from, v_uv), texture(tve_to, v_uv), 0.5);
}
