#version 440

// Declares no "strength" uniform although the manifest has that param.

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
};

layout(binding = 1) uniform sampler2D tve_source;

void main()
{
    fragColor = texture(tve_source, v_uv);
}
