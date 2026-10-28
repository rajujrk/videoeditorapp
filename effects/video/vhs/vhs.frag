#version 440

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    float amount;
    float jitter;
};

layout(binding = 1) uniform sampler2D tve_source;

float hash(vec2 p)
{
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

// A worn videotape: lines that slip sideways, colour bleeding to the right, scanlines, a
// rolling band of noise, and washed-out colour.
void main()
{
    float line = floor(v_uv.y / tve_texelSize.y / 2.0);
    float frame = floor(tve_time * 30.0);
    float slip = (hash(vec2(line, frame)) - 0.5) * jitter * 0.01;
    float band = smoothstep(0.06, 0.0, abs(fract(v_uv.y - tve_time * 0.15) - 0.5));
    slip += band * jitter * 0.03 * (hash(vec2(frame, line * 0.1)) - 0.5);
    vec2 uv = vec2(v_uv.x + slip * amount, v_uv.y);
    vec4 source = texture(tve_source, uv);
    float bleed = tve_texelSize.x * 4.0 * amount;
    vec3 c = vec3(texture(tve_source, uv - vec2(bleed, 0.0)).r, source.g, texture(tve_source, uv + vec2(bleed, 0.0)).b);
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = mix(c, vec3(luma), 0.3 * amount);
    c *= 1.0 - 0.18 * amount * step(0.5, fract(v_uv.y / tve_texelSize.y / 2.0));
    c += band * amount * 0.25 * (hash(v_uv * 731.0 + frame) - 0.3);
    fragColor = vec4(clamp(c, 0.0, 1.0), source.a);
}
