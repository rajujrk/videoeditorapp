#version 440

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    float amount;
    float size;     // distance (0 = center, 1 = corner) where the falloff ends
    float softness; // width of the falloff
    vec4 color;
};

layout(binding = 1) uniform sampler2D tve_source;

void main()
{
    vec4 source = texture(tve_source, v_uv);
    // Aspect-correct distance from the center, normalized so the corners are at 1.
    float aspect = tve_texelSize.y / tve_texelSize.x;
    vec2 d = (v_uv - 0.5) * vec2(aspect, 1.0);
    float dist = length(d) / length(vec2(aspect, 1.0) * 0.5);
    float edge = smoothstep(size - softness, size, dist);
    fragColor = vec4(mix(source.rgb, color.rgb, edge * amount * color.a), source.a);
}
