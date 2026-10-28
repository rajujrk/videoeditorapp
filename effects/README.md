# Built-in effects and transitions

Each effect is a folder under `video/` holding a `manifest.json` and one GLSL fragment
shader per pass; each transition is a folder under `transitions/` with a manifest of kind
`transition` and exactly one pass (see [Transitions](#transitions)). The build compiles the shaders with Qt's `qsb` (Metal, Vulkan, D3D and
OpenGL variants) and embeds everything as Qt resources. A shader that does not compile
fails the build; a manifest that does not validate, or a parameter the shaders do not
declare, is reported when the effect library loads and the effect is skipped.

## Manifest

See [effect-manifest.schema.json](effect-manifest.schema.json) for editor help and
`core/content/EffectManifest.cpp` for the authoritative rules.

| Param type | Value | Shader member |
|---|---|---|
| `float` | number in `[min, max]` | `float` |
| `int` | integer in `[min, max]` | `int` or `float` |
| `bool` | `true` / `false` | `int` (0/1) or `float` |
| `enum` | one of `values`; passed as its index | `int` or `float` |
| `color` | `[r, g, b]` or `[r, g, b, a]`, 0–1 | `vec4` or `vec3` |

## Shader contract

Every pass runs the shared vertex shader `common/fullscreen.vert` and a fragment shader
that starts like this:

```glsl
#version 440

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize; // 1 / input size in pixels
    float tve_time;     // seconds since playback started
    float tve_flipY;    // used by the vertex shader
    // then any of the effect's params, named exactly like their manifest ids
};

layout(binding = 1) uniform sampler2D tve_source; // output of the previous pass
```

The four `tve_` members must come first, in this order. Parameters are matched to
uniform members by name through shader reflection, so their order and std140 padding
do not matter. Pass `n` reads the output of pass `n - 1`; the first pass reads the
effect's input. All passes render at the input's resolution.

## Built-in set (Phase 0)

| Folder | Id | Passes | Params |
|---|---|---|---|
| `color_adjust` | `tve.color.adjust` | 1 | brightness, contrast, saturation, invert |
| `blur_gaussian` | `tve.blur.gaussian` | 2 (separable) | radius |
| `vignette` | `tve.stylize.vignette` | 1 | amount, size, softness, color |
| `rgb_split` | `tve.glitch.rgb_split` | 1 | amount, angle, channels |
| `pixelate` | `tve.stylize.pixelate` | 1 | blockSize |

## Transitions

A transition package is a manifest with `"kind": "transition"` and one pass. Its shader
blends two pictures of the same size (the outgoing and incoming clips, each already placed
on a transparent canvas) by `tve_progress`, which runs from 0 (all outgoing) to 1 (all
incoming):

```glsl
layout(std140, binding = 0) uniform EffectParams {
    vec2 tve_texelSize;
    float tve_time;
    float tve_flipY;
    float tve_progress; // 0 → 1 across the transition
    // then any of the transition's params, named exactly like their manifest ids
};

layout(binding = 1) uniform sampler2D tve_from; // outgoing clip
layout(binding = 2) uniform sampler2D tve_to;   // incoming clip
```

Colours are straight (not premultiplied) alpha; outside a picture's edges it is
transparent. `v_uv` has (0, 0) at the top-left of the picture and y pointing down, so
"up" is −y. Every transition must show exactly the outgoing picture at progress 0 and the
incoming one at progress 1; `tve_gpu_tests` checks this for every package. Packages are
original work (`"license": "TVE-Original"`).

| Folder | Id | Params |
|---|---|---|
| `dissolve` | `tve.transition.dissolve` | — |
| `dip_to_black`, `dip_to_white` | `tve.transition.dip_to_black`, `…dip_to_white` | color |
| `flash` | `tve.transition.flash` | intensity |
| `push`, `slide`, `reveal` | `tve.transition.push`, `…slide`, `…reveal` | direction |
| `squeeze` | `tve.transition.squeeze` | orientation |
| `wipe` | `tve.transition.wipe` | direction, softness |
| `split` | `tve.transition.split` | orientation, softness |
| `radial_wipe` | `tve.transition.radial_wipe` (Clock Wipe) | softness |
| `blinds` | `tve.transition.blinds` | count, orientation, softness |
| `checkerboard` | `tve.transition.checkerboard` | squares |
| `iris` | `tve.transition.iris` | softness |
| `zoom_in`, `cross_zoom` | `tve.transition.zoom_in`, `…cross_zoom` | strength |
| `spin` | `tve.transition.spin` | turns |
| `pixelate` | `tve.transition.pixelate` | blockSize |
| `blur` | `tve.transition.blur` | radius |
| `ripple` | `tve.transition.ripple` | amplitude, waves |
| `door` | `tve.transition.door` | — |

## Overlays and looks

Packages whose `category` includes `"overlay"` are listed after the other effects and
marked "Overlay" in the library: procedural layers drawn over the picture (light leaks,
grain, dust and scratches, VHS, a viewfinder, bokeh), animated with `tve_time`.

`looks/looks.json` lists the Filters library: each look is a colour grade (the
`ColorGrade` JSON of a project file) and usually a 3D LUT in `looks/luts/`, both generated
by `tools/make_looks.py`. Built-in LUT paths start `:/effects/looks/luts/`.

