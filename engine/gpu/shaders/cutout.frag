#version 440

// Portrait cutout: the picture kept where the person matte (from Vision, smaller than the
// picture, sampled smoothly) says there is a person — or, inverted, everywhere else. The
// edge is a soft step across the matte's 0.5 level, moved out or in by the shift.

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    vec4 params; // x: softness 0..1, y: shift -1..1, z: invert (0/1)
};

layout(binding = 1) uniform sampler2D tve_source;
layout(binding = 2) uniform sampler2D matte;

void main()
{
    vec4 c = texture(tve_source, v_uv);
    float m = texture(matte, v_uv).r;
    float edge = 0.5 - 0.4 * params.y;
    float width = mix(0.01, 0.45, params.x);
    float kept = smoothstep(edge - width, edge + width, m);
    if (params.z > 0.5)
        kept = 1.0 - kept;
    fragColor = vec4(c.rgb, c.a * kept);
}
