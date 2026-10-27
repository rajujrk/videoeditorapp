#version 440

layout(location = 0) in vec2 v_texCoord;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 yuvToRgb;
    vec4 scaleFlip;
};

layout(binding = 1) uniform sampler2D lumaTex;   // Y plane
layout(binding = 2) uniform sampler2D chromaTex; // interleaved CbCr plane, half resolution

void main()
{
    vec4 yuv = vec4(texture(lumaTex, v_texCoord).r, texture(chromaTex, v_texCoord).rg, 1.0);
    fragColor = vec4((yuvToRgb * yuv).rgb, 1.0);
}
