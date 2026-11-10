#version 440

// 360° video (model/Sphere.h, which this mirrors): each output pixel's direction — a
// rectilinear view's ray (mode 0, reframing) or an equirectangular point's (mode 1, turning
// the sphere) — rotated by the view and looked up in the equirectangular source. u is the
// longitude (the front in the middle), v the latitude (top first); x right, y up, z forward.

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    vec4 row0; // the rotation's rows (xyz)
    vec4 row1;
    vec4 row2;
    vec4 params; // x: mode, y: tan(fov / 2) across, z: the same up
};

layout(binding = 1) uniform sampler2D tve_source;

const float kPi = 3.14159265358979;

void main()
{
    vec3 d;
    if (params.x < 0.5) {
        d = vec3((2.0 * v_uv.x - 1.0) * params.y, (1.0 - 2.0 * v_uv.y) * params.z, 1.0);
    } else {
        float lon = (v_uv.x - 0.5) * 2.0 * kPi;
        float lat = (0.5 - v_uv.y) * kPi;
        d = vec3(cos(lat) * sin(lon), sin(lat), cos(lat) * cos(lon));
    }
    vec3 r = normalize(vec3(dot(row0.xyz, d), dot(row1.xyz, d), dot(row2.xyz, d)));
    vec2 source = vec2(atan(r.x, r.z) / (2.0 * kPi) + 0.5, 0.5 - asin(clamp(r.y, -1.0, 1.0)) / kPi);
    // Across the seam the sampler wraps; the mip-less derivative jump there is harmless.
    fragColor = textureLod(tve_source, source, 0.0);
}
