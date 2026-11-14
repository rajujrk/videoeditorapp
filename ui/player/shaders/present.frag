#version 440

// Draws the processed frame into the item (aspect-fit is done by video.vert): left of the
// split the picture before its grade (when comparing), and a checkerboard under what is
// transparent (when the grid is on).

layout(location = 0) in vec2 v_texCoord;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 yuvToRgb; // unused here; keeps the block identical to video.vert's
    vec4 scaleFlip;
    vec4 options; // x: the split (0..1; < 0: not comparing), y: grid on, z: a grid cell (pixels)
};

layout(binding = 1) uniform sampler2D image;
layout(binding = 2) uniform sampler2D before;

void main()
{
    vec4 c = options.x >= 0.0 && v_texCoord.x < options.x ? texture(before, v_texCoord) : texture(image, v_texCoord);
    vec3 rgb = c.rgb;
    if (options.y > 0.5) {
        vec2 cell = floor(gl_FragCoord.xy / max(options.z, 1.0));
        float odd = mod(cell.x + cell.y, 2.0);
        vec3 checker = vec3(mix(0.80, 0.62, odd));
        rgb = mix(checker, c.rgb, clamp(c.a, 0.0, 1.0));
    }
    fragColor = vec4(rgb, 1.0);
}
