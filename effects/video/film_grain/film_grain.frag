#version 440

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    float amount;
    float size;
};

layout(binding = 1) uniform sampler2D tve_source;

float hash(vec2 p)
{
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

// Moving photographic grain, strongest in the midtones.
void main()
{
    vec4 source = texture(tve_source, v_uv);
    vec2 cell = floor(v_uv / (tve_texelSize * size));
    float frame = floor(tve_time * 24.0);
    float n = hash(cell + frame * 17.31) + hash(cell * 1.7 - frame * 3.1) - 1.0; // -1..1, triangular
    float luma = dot(source.rgb, vec3(0.2126, 0.7152, 0.0722));
    float midtones = 1.0 - abs(luma - 0.5) * 1.4;
    fragColor = vec4(clamp(source.rgb + n * amount * 0.18 * midtones, 0.0, 1.0), source.a);
}
