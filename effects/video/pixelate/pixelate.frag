#version 440

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    int blockSize;
};

layout(binding = 1) uniform sampler2D tve_source;

void main()
{
    vec2 size = 1.0 / tve_texelSize;
    float block = float(max(blockSize, 1));
    // Sample the texel center nearest the middle of this pixel's block.
    vec2 origin = floor(v_uv * size / block) * block;
    vec2 texel = min(origin + floor(block * 0.5), size - 1.0) + 0.5;
    fragColor = texture(tve_source, texel / size);
}
