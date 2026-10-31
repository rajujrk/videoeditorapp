#version 440

// Cursor effects on a screen recording: a spotlight (everything dimmed but a disc around
// the cursor), a highlight (a soft yellow disc around it) and a ring that grows and fades
// after a click. Positions are picture fractions; sizes are fractions of the picture's height.

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    vec4 cursor; // xy: where it is; z: radius; w: ring progress 0..1 (< 0: no ring)
    vec4 flags;  // x: highlight, y: spotlight
};

layout(binding = 1) uniform sampler2D tve_source;

void main()
{
    vec4 c = texture(tve_source, v_uv);
    // Distance from the cursor in picture heights (so circles stay round).
    vec2 aspect = vec2(tve_texelSize.y / tve_texelSize.x, 1.0);
    float d = length((v_uv - cursor.xy) * aspect);
    float r = cursor.z;
    vec3 rgb = c.rgb;
    if (flags.y > 0.5)
        rgb *= mix(1.0, 0.35, smoothstep(r * 1.2, r * 1.6, d));
    if (flags.x > 0.5)
        rgb = mix(rgb, vec3(1.0, 0.86, 0.2), 0.35 * (1.0 - smoothstep(r * 0.75, r, d)));
    if (cursor.w >= 0.0) {
        float ringRadius = mix(r * 0.3, r * 1.4, cursor.w);
        float width = r * 0.14;
        float ring = 1.0 - smoothstep(width * 0.5, width, abs(d - ringRadius));
        rgb = mix(rgb, vec3(1.0, 0.86, 0.2), ring * (1.0 - cursor.w));
    }
    fragColor = vec4(rgb, c.a);
}
