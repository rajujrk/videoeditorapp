#version 440

// Cinematic depth of field: each pixel blurred by a disc whose size grows with how far its
// depth is from the focus (beyond the range kept sharp), like a lens wide open. Gathered over
// 48 samples; a neighbour counts only if its own blur reaches this pixel, so something sharp
// does not bleed into the blur around it.

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    vec4 params; // x: most blur (a disc's radius, in picture heights), y: focus nearness 0..1,
                 // z: range kept sharp 0..1
};

layout(binding = 1) uniform sampler2D tve_source;
layout(binding = 2) uniform sampler2D depth; // nearness 0..1 (smaller; sampled smoothly)

// How blurred a point is, 0..1.
float blurAt(vec2 uv)
{
    float d = abs(texture(depth, uv).r - params.y);
    return smoothstep(params.z, params.z + 0.25, d);
}

void main()
{
    vec4 c = texture(tve_source, v_uv);
    float most = max(params.x, 0.0);
    float own = blurAt(v_uv);
    if (most <= 0.0 || own <= 0.001) {
        fragColor = c;
        return;
    }
    vec2 aspect = vec2(tve_texelSize.x / tve_texelSize.y, 1.0);
    float radius = own * most; // in picture heights
    vec4 sum = c;
    float total = 1.0;
    // Three rings of 8, 16 and 24 samples.
    for (int ring = 1; ring <= 3; ++ring) {
        float r = float(ring) / 3.0;
        int count = ring * 8;
        for (int i = 0; i < count; ++i) {
            float a = (float(i) + 0.5 * float(ring)) * 6.2831853 / float(count);
            vec2 offset = vec2(cos(a), sin(a)) * radius * r;
            vec2 uv = v_uv + offset * aspect;
            float theirs = blurAt(uv) * most;
            // Counts when its blur reaches this far (and less the sharper it is).
            float w = smoothstep(0.0, 1.0, theirs / max(radius * r, 1e-5));
            sum += texture(tve_source, uv) * w;
            total += w;
        }
    }
    fragColor = sum / total;
}
