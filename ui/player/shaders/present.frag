#version 440

// Draws the processed frame into the item (aspect-fit is done by video.vert).

layout(location = 0) in vec2 v_texCoord;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 yuvToRgb; // unused here; keeps the block identical to video.vert's
    vec4 scaleFlip;
};

layout(binding = 1) uniform sampler2D image;

void main()
{
    fragColor = vec4(texture(image, v_texCoord).rgb, 1.0);
}
