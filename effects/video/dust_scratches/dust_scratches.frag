#version 440

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    float amount;
    int scratches;
};

layout(binding = 1) uniform sampler2D tve_source;

float hash(vec2 p)
{
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

// Old film: thin vertical scratches that wander between frames, and specks of dust.
void main()
{
    vec4 source = texture(tve_source, v_uv);
    float frame = floor(tve_time * 12.0);
    vec3 c = source.rgb;
    for (int i = 0; i < 8; ++i) {
        if (i >= scratches)
            break;
        float seed = float(i) * 7.13 + frame;
        if (hash(vec2(seed, 1.0)) > 0.65)
            continue; // not on this frame
        float x = hash(vec2(seed, 2.0)) + 0.01 * sin(v_uv.y * 30.0 + seed);
        float width = tve_texelSize.x * (1.0 + 2.0 * hash(vec2(seed, 3.0)));
        float line = 1.0 - smoothstep(0.0, width, abs(v_uv.x - x));
        float bright = hash(vec2(seed, 4.0)) > 0.5 ? 1.0 : -1.0;
        c += bright * line * 0.35 * amount;
    }
    // Dust: a few dark or light blobs per frame.
    vec2 grid = floor(v_uv * vec2(40.0, 24.0));
    float speck = hash(grid + frame * 5.7);
    if (speck > 1.0 - 0.012 * amount) {
        vec2 centre = (grid + vec2(hash(grid + 1.3), hash(grid + 2.9))) / vec2(40.0, 24.0);
        float d = length((v_uv - centre) / tve_texelSize);
        c -= (1.0 - smoothstep(1.0, 3.5, d)) * 0.6 * amount;
    }
    fragColor = vec4(clamp(c, 0.0, 1.0), source.a);
}
