#version 440

// Faces covered: inside each face's ellipse (with a soft rim) the picture is shown as a
// mosaic of blocks, blurred, or filled — whichever the clip chose. Face boxes are picture
// fractions (centre, size); the block or blur size follows each face's size.

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    vec4 params;    // x: face count, y: style (0 mosaic, 1 blur, 2 fill), z: amount 0..1
    vec4 faces[16]; // centre xy, size xy
};

layout(binding = 1) uniform sampler2D tve_source;

void main()
{
    vec4 c = texture(tve_source, v_uv);
    int count = int(params.x + 0.5);
    float cover = 0.0;
    int which = 0;
    for (int i = 0; i < 16; ++i) {
        if (i >= count)
            break;
        vec2 d = (v_uv - faces[i].xy) / max(faces[i].zw * 0.5, vec2(1e-4));
        float inside = 1.0 - smoothstep(0.85, 1.0, length(d));
        if (inside > cover) {
            cover = inside;
            which = i;
        }
    }
    if (cover <= 0.0) {
        fragColor = c;
        return;
    }
    vec2 size = faces[which].zw;
    float faceSide = min(size.x / tve_texelSize.x, size.y / tve_texelSize.y); // pixels
    vec4 covered;
    if (params.y < 0.5) {
        // Blocks of a twelfth (amount 0) to a third (amount 1) of the face.
        float block = max(3.0, faceSide * mix(0.08, 0.33, params.z));
        vec2 pixel = v_uv / tve_texelSize;
        vec2 centre = (floor(pixel / block) + 0.5) * block;
        covered = texture(tve_source, centre * tve_texelSize);
    } else if (params.y < 1.5) {
        // A wide blur: 13 × 13 samples over a radius of a tenth to half the face.
        float radius = max(2.0, faceSide * mix(0.1, 0.5, params.z));
        vec4 sum = vec4(0.0);
        float total = 0.0;
        for (int y = -6; y <= 6; ++y) {
            for (int x = -6; x <= 6; ++x) {
                vec2 o = vec2(float(x), float(y)) / 6.0;
                float w = exp(-2.0 * dot(o, o));
                sum += texture(tve_source, v_uv + o * radius * tve_texelSize) * w;
                total += w;
            }
        }
        covered = sum / total;
    } else {
        covered = vec4(vec3(0.08), c.a);
    }
    fragColor = mix(c, vec4(covered.rgb, c.a), cover);
}
