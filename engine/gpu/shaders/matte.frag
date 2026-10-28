#version 440

// Track matte: a layer (already placed on its own transparent canvas) keeps what the matte
// (likewise placed) lets through: its alpha or luma, or their inverse.

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    int mode; // MatteMode: 1 alpha, 2 inverted alpha, 3 luma, 4 inverted luma
};

layout(binding = 1) uniform sampler2D tve_source; // the layer
layout(binding = 2) uniform sampler2D matte;

void main()
{
    vec4 layer = texture(tve_source, v_uv);
    vec4 m = texture(matte, v_uv);
    float luma = dot(m.rgb, vec3(0.2126, 0.7152, 0.0722)) * m.a;
    float through = mode == 1 ? m.a : mode == 2 ? 1.0 - m.a : mode == 3 ? luma : 1.0 - luma;
    fragColor = vec4(layer.rgb, layer.a * through);
}
