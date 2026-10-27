#version 440

// Draws one layer over the canvas: samples the canvas so far and the layer (through the
// canvas→layer mapping from LayerPlacement), then blends with opacity and blend mode.

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    vec4 mapU;     // layer.u = dot(mapU.xyz, vec3(uv, 1))
    vec4 mapV;     // layer.v = dot(mapV.xyz, vec3(uv, 1))
    vec4 cropRect; // left, top, right, bottom of the visible layer region
    float opacity;
    int blendMode; // BlendMode order: normal, multiply, screen, overlay, add, darken, lighten
};

layout(binding = 1) uniform sampler2D tve_source; // canvas so far
layout(binding = 2) uniform sampler2D layerTex;

vec3 blendColor(vec3 base, vec3 top)
{
    if (blendMode == 1)
        return base * top;
    if (blendMode == 2)
        return 1.0 - (1.0 - base) * (1.0 - top);
    if (blendMode == 3)
        return mix(2.0 * base * top, 1.0 - 2.0 * (1.0 - base) * (1.0 - top), step(0.5, base));
    if (blendMode == 4)
        return base + top;
    if (blendMode == 5)
        return min(base, top);
    if (blendMode == 6)
        return max(base, top);
    return top;
}

void main()
{
    vec4 canvas = texture(tve_source, v_uv);
    vec3 p = vec3(v_uv, 1.0);
    vec2 layerUv = vec2(dot(mapU.xyz, p), dot(mapV.xyz, p));
    if (layerUv.x < cropRect.x || layerUv.y < cropRect.y || layerUv.x > cropRect.z || layerUv.y > cropRect.w) {
        fragColor = canvas;
        return;
    }
    vec4 layer = texture(layerTex, layerUv);
    float alpha = layer.a * opacity;
    fragColor = vec4(mix(canvas.rgb, blendColor(canvas.rgb, layer.rgb), alpha), max(canvas.a, alpha));
}
