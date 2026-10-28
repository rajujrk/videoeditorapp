#version 440

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    float amount;
    float size;
    float speed;
};

layout(binding = 1) uniform sampler2D tve_source;

float hash(vec2 p)
{
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

// Out-of-focus lights drifting upwards: soft discs of warm and cool light, screened over
// the picture.
void main()
{
    vec4 source = texture(tve_source, v_uv);
    float aspect = tve_texelSize.y / tve_texelSize.x;
    vec2 p = vec2(v_uv.x * aspect, v_uv.y + tve_time * speed * 0.05);
    float cells = 1.0 / max(size * 2.5, 0.05);
    vec2 cell = floor(p * cells);
    vec3 light = vec3(0.0);
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 id = cell + vec2(x, y);
            if (hash(id + 0.5) > 0.55)
                continue; // empty cell
            vec2 centre = (id + vec2(hash(id + 1.7), hash(id + 9.2))) / cells;
            float radius = size * (0.5 + 0.7 * hash(id + 4.4));
            float disc = 1.0 - smoothstep(radius * 0.8, radius, length(p - centre));
            vec3 tint = mix(vec3(1.0, 0.78, 0.45), vec3(0.6, 0.8, 1.0), hash(id + 3.3));
            light += disc * tint * (0.25 + 0.35 * hash(id + 6.1));
        }
    }
    light = clamp(light * amount, 0.0, 1.0);
    fragColor = vec4(1.0 - (1.0 - source.rgb) * (1.0 - light), source.a);
}
