#version 440

// A clip's chroma key and masks: colours near the key colour turn transparent (with the
// screen's colour cast taken off what stays), then alpha is multiplied by the union of the
// masks. Mask geometry is in picture fractions; distances are worked out in pixels.

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    vec4 key;         // rgb: key colour; a: 1 when keying
    vec4 keyParams;   // x: tolerance, y: softness, z: spill, w: mask count
    vec4 maskPlace[8]; // centre xy, size xy (picture fractions)
    vec4 maskShape[8]; // rotation (radians), roundness, feather (pixels), invert (0/1)
    vec4 maskKind[8];  // shape (0 rect, 1 ellipse, 2 line, 3 star, 4 heart, 5 pen), first point, point count
    vec4 points[16];   // pen corners, two per vec4 (picture fractions)
};

layout(binding = 1) uniform sampler2D tve_source;

vec2 cbcr(vec3 c)
{
    float y = dot(c, vec3(0.2126, 0.7152, 0.0722));
    return vec2((c.b - y) / 1.8556, (c.r - y) / 1.5748);
}

vec2 penPoint(int i)
{
    vec4 pair = points[i / 2];
    return (i % 2 == 0) ? pair.xy : pair.zw;
}

// Signed distances (negative inside), in pixels.
float sdRoundBox(vec2 p, vec2 halfSize, float r)
{
    vec2 q = abs(p) - halfSize + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

float sdEllipse(vec2 p, vec2 halfSize)
{
    // Scaled circle: close enough for a soft edge.
    float k = min(halfSize.x, halfSize.y);
    return (length(p / max(halfSize, vec2(1e-3))) - 1.0) * k;
}

float sdStar(vec2 p, float r)
{
    // A five-pointed star, point up (y up here), its points reaching r.
    const vec2 k1 = vec2(0.809016994, -0.587785252);
    const vec2 k2 = vec2(-k1.x, k1.y);
    const float inner = 0.45;
    p.x = abs(p.x);
    p -= 2.0 * max(dot(k1, p), 0.0) * k1;
    p -= 2.0 * max(dot(k2, p), 0.0) * k2;
    p.x = abs(p.x);
    p.y -= r;
    vec2 ba = inner * vec2(-k1.y, k1.x) - vec2(0.0, 1.0);
    float h = clamp(dot(p, ba) / dot(ba, ba), 0.0, r);
    return length(p - ba * h) * sign(p.y * ba.x - p.x * ba.y);
}

float sdHeart(vec2 p, float size)
{
    // A heart, point down, about `size` tall, centred (y up here).
    p = p / size + vec2(0.0, 0.5);
    p.x = abs(p.x);
    float d;
    if (p.y + p.x > 1.0) {
        d = length(p - vec2(0.25, 0.75)) - sqrt(2.0) / 4.0;
    } else {
        vec2 a = p - vec2(0.0, 1.0);
        vec2 b = p - 0.5 * max(p.x + p.y, 0.0);
        d = sqrt(min(dot(a, a), dot(b, b))) * sign(p.x - p.y);
    }
    return d * size;
}

float sdPolygon(vec2 p, int first, int count, vec2 pictureSize)
{
    vec2 v0 = penPoint(first) * pictureSize;
    float d = dot(p - v0, p - v0);
    float s = 1.0;
    for (int k = 0; k < 32; ++k) {
        if (k >= count)
            break;
        int i = first + k;
        int j = first + (k == 0 ? count - 1 : k - 1);
        vec2 vi = penPoint(i) * pictureSize;
        vec2 vj = penPoint(j) * pictureSize;
        vec2 e = vj - vi;
        vec2 w = p - vi;
        vec2 b = w - e * clamp(dot(w, e) / max(dot(e, e), 1e-6), 0.0, 1.0);
        d = min(d, dot(b, b));
        bvec3 c = bvec3(p.y >= vi.y, p.y < vj.y, e.x * w.y > e.y * w.x);
        if (all(c) || all(not(c)))
            s *= -1.0;
    }
    return s * sqrt(d);
}

float coverage(int m, vec2 pixel, vec2 pictureSize)
{
    vec4 place = maskPlace[m];
    vec4 shape = maskShape[m];
    int kind = int(maskKind[m].x + 0.5);
    float d;
    if (kind == 5) {
        d = sdPolygon(pixel, int(maskKind[m].y + 0.5), int(maskKind[m].z + 0.5), pictureSize);
    } else {
        vec2 centre = place.xy * pictureSize;
        vec2 halfSize = place.zw * pictureSize * 0.5;
        float c = cos(shape.x);
        float s = sin(shape.x);
        vec2 q = pixel - centre;
        q = vec2(c * q.x + s * q.y, -s * q.x + c * q.y); // into the shape's own frame
        if (kind == 0)
            d = sdRoundBox(q, halfSize, shape.y * min(halfSize.x, halfSize.y));
        else if (kind == 1)
            d = sdEllipse(q, halfSize);
        else if (kind == 2)
            d = q.y; // a line: everything above it
        else if (kind == 3)
            d = sdStar(vec2(q.x, -q.y), min(halfSize.x, halfSize.y));
        else
            d = sdHeart(vec2(q.x, -q.y), min(halfSize.x, halfSize.y) * 2.0);
    }
    float feather = max(shape.z, 0.75);
    float inside = 1.0 - smoothstep(-feather, feather, d);
    return shape.w > 0.5 ? 1.0 - inside : inside;
}

void main()
{
    vec4 color = texture(tve_source, v_uv);
    float alpha = color.a;
    if (key.a > 0.5) {
        float distance = length(cbcr(color.rgb) - cbcr(key.rgb)) * 2.0;
        float kept = smoothstep(keyParams.x, keyParams.x + max(keyParams.y, 1e-4), distance);
        alpha *= kept;
        // Spill: the key's strongest channel may not stand above the other two.
        if (key.g >= key.r && key.g >= key.b)
            color.g = mix(color.g, min(color.g, max(color.r, color.b)), keyParams.z);
        else if (key.b >= key.r)
            color.b = mix(color.b, min(color.b, max(color.r, color.g)), keyParams.z);
        else
            color.r = mix(color.r, min(color.r, max(color.g, color.b)), keyParams.z);
    }
    int count = int(keyParams.w + 0.5);
    if (count > 0) {
        vec2 pictureSize = 1.0 / tve_texelSize;
        vec2 pixel = v_uv * pictureSize;
        float covered = 0.0;
        for (int m = 0; m < 8; ++m) {
            if (m >= count)
                break;
            covered = max(covered, coverage(m, pixel, pictureSize));
        }
        alpha *= covered;
    }
    fragColor = vec4(color.rgb, alpha);
}
