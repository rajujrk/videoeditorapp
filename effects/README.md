# Built-in effects, transitions, audio effects and stickers

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

Two optional additions, found by reflection:

- `layout(binding = 2) uniform sampler2D tve_input;` — the effect's own input, in any pass
  (a glow adds its blurred light back to the picture, a shadow puts the picture over its
  blurred shape). The chain keeps that texture intact until the effect's last pass.
- `vec4 tve_bounds;` right after `tve_flipY` — for canvas effects, where the clip is on the
  canvas (left, top, right, bottom, 0–1); `(0, 0, 1, 1)` for picture effects.

### Picture and canvas effects

A video effect's manifest may set `"space"`: `"picture"` (the default) runs it on the clip's
own picture, before it is placed — after its colour, key and masks. `"canvas"` runs it after
placement, on a transparent canvas of the project's size holding just that clip (moved,
scaled, rotated, cropped, faded), so it can draw outside the picture: shadows, outlines,
glows, fills. A clip's picture effects run first, then its canvas effects, each group in the
stack's order; the result is drawn with the clip's blend mode. On an adjustment layer every
effect works on the canvas below.

## Built-in set

61 effects, all original (`"license": "TVE-Original"`), listed by their first category; the
library shows them in this order with category chips and GPU-rendered previews
(`image://tveeffect/<id>@<seconds>`). Sizes are percentages of the picture's height unless a
title says pixels, so an effect looks the same on a proxy, a preview and the export.
`tve_gpu_tests` runs every package at its defaults and checks it changes the picture and
keeps colours in range.

### Blur & sharpen (9)

| Title | Folder | Id | Passes | Params |
|---|---|---|---|---|
| Directional Blur | `motion_blur` | `tve.blur.directional` | 1 | strength, angle |
| Frosted Glass | `frosted_glass` | `tve.blur.frosted_glass` | 1 | amount |
| Gaussian Blur | `blur_gaussian` | `tve.blur.gaussian` | 2 | radius |
| Lens Blur | `lens_blur` | `tve.blur.lens` | 1 | radius, bokeh |
| Radial Blur | `zoom_blur` | `tve.blur.radial` | 1 | strength, centerX, centerY |
| Sharpen | `sharpen` | `tve.blur.sharpen` | 1 | amount, radius |
| Smooth | `smooth` | `tve.blur.smooth` | 1 | strength, radius |
| Spin Blur | `spin_blur` | `tve.blur.spin` | 1 | angle, centerX, centerY |
| Tilt Shift | `tilt_shift` | `tve.blur.tilt_shift` | 2 | position, width, softness, blur, saturation |

### Colour (10)

| Title | Folder | Id | Passes | Params |
|---|---|---|---|---|
| Black & White | `black_white` | `tve.color.black_white` | 1 | channel, contrast, intensity |
| Color Adjust | `color_adjust` | `tve.color.adjust` | 1 | brightness, contrast, saturation, invert |
| Colour Splash | `color_splash` | `tve.color.splash` | 1 | hue, range, softness, desaturate |
| Duotone | `duotone` | `tve.color.duotone` | 1 | shadows, highlights, contrast, intensity |
| Gradient Map | `gradient_map` | `tve.color.gradient_map` | 1 | shadows, midtones, highlights, intensity |
| Hue Rotate | `hue_rotate` | `tve.color.hue_rotate` | 1 | angle, speed |
| Posterize | `posterize` | `tve.color.posterize` | 1 | levels, amount |
| Solarize | `solarize` | `tve.color.solarize` | 1 | threshold, intensity |
| Threshold | `threshold` | `tve.color.threshold` | 1 | level, softness, dark, light |
| Tint | `tint` | `tve.color.tint` | 1 | color, amount |

### Stylize (12)

| Title | Folder | Id | Passes | Params |
|---|---|---|---|---|
| CRT Screen | `crt` | `tve.stylize.crt` | 1 | curvature, scanlines, mask, vignette |
| Cartoon | `cartoon` | `tve.stylize.cartoon` | 1 | levels, threshold, edgeWidth |
| Crystallize | `crystallize` | `tve.stylize.crystallize` | 1 | cellSize |
| Emboss | `emboss` | `tve.stylize.emboss` | 1 | strength, angle, colour |
| Find Edges | `edge_detect` | `tve.stylize.edges` | 1 | strength, mode |
| Halftone | `halftone` | `tve.stylize.halftone` | 1 | dotSize, angle, mode |
| LED Board | `led_board` | `tve.stylize.led_board` | 1 | cellSize, gap, glow |
| Neon Edges | `neon_edges` | `tve.stylize.neon_edges` | 1 | strength, mode, color, background |
| Oil Paint | `oil_paint` | `tve.stylize.oil_paint` | 1 | radius |
| Pencil Sketch | `sketch` | `tve.stylize.sketch` | 1 | strength, shading, paper |
| Pixelate | `pixelate` | `tve.stylize.pixelate` | 1 | blockSize |
| Vignette | `vignette` | `tve.stylize.vignette` | 1 | amount, size, softness, color |

### Distort (11)

| Title | Folder | Id | Passes | Params |
|---|---|---|---|---|
| Bulge & Pinch | `bulge` | `tve.distort.bulge` | 1 | amount, radius, centerX, centerY |
| Camera Shake | `shake` | `tve.distort.shake` | 1 | amount, speed, rotation |
| Flip | `flip` | `tve.distort.flip` | 1 | mode |
| Heat Haze | `heat_haze` | `tve.distort.heat_haze` | 1 | amount, scale, speed |
| Kaleidoscope | `kaleidoscope` | `tve.distort.kaleidoscope` | 1 | segments, angle, zoom, centerX, centerY |
| Lens Distortion | `lens_distortion` | `tve.distort.lens` | 1 | amount, zoom, fringe |
| Little Planet | `little_planet` | `tve.distort.little_planet` | 1 | zoom, rotation |
| Mirror | `mirror` | `tve.distort.mirror` | 1 | mode |
| Swirl | `swirl` | `tve.distort.swirl` | 1 | angle, radius, centerX, centerY |
| Water Ripple | `ripple` | `tve.distort.ripple` | 1 | amplitude, frequency, speed, damping, centerX, centerY |
| Wave | `wave` | `tve.distort.wave` | 1 | amplitude, wavelength, speed, direction |

### Glitch (3)

| Title | Folder | Id | Passes | Params |
|---|---|---|---|---|
| Bad Signal | `bad_signal` | `tve.glitch.bad_signal` | 1 | noise, jitter, rolling, speed |
| Digital Glitch | `digital_glitch` | `tve.glitch.digital` | 1 | amount, blockSize, speed, colourShift |
| RGB Split | `rgb_split` | `tve.glitch.rgb_split` | 1 | amount, angle, channels |

### Light (5)

| Title | Folder | Id | Passes | Params |
|---|---|---|---|---|
| Glow | `glow` | `tve.light.glow` | 3 | threshold, radius, intensity, tint |
| Lens Flare | `lens_flare` | `tve.light.lens_flare` | 1 | positionX, positionY, intensity, color |
| Light Rays | `light_rays` | `tve.light.rays` | 1 | centerX, centerY, reach, intensity, threshold |
| Spotlight | `spotlight` | `tve.light.spotlight` | 1 | centerX, centerY, radius, softness, darkness, color |
| Strobe | `strobe` | `tve.light.strobe` | 1 | frequency, duty, intensity, color |

### Frame (5)

| Title | Folder | Id | Passes | Params |
|---|---|---|---|---|
| Background Fill | `background_fill` | `tve.frame.background_fill` | 3 (canvas) | mode, blur, dim, color |
| Drop Shadow | `drop_shadow` | `tve.frame.drop_shadow` | 3 (canvas) | offset, angle, blur, opacity, color |
| Outer Glow | `outer_glow` | `tve.frame.outer_glow` | 3 (canvas) | size, intensity, color |
| Outline | `outline` | `tve.frame.outline` | 1 (canvas) | width, color, opacity |
| Rounded Corners | `rounded_corners` | `tve.frame.rounded_corners` | 1 | radius |

### Overlay (6)

| Title | Folder | Id | Passes | Params |
|---|---|---|---|---|
| Bokeh | `bokeh` | `tve.overlay.bokeh` | 1 | amount, size, speed |
| Dust & Scratches | `dust_scratches` | `tve.overlay.dust_scratches` | 1 | amount, scratches |
| Film Grain | `film_grain` | `tve.overlay.film_grain` | 1 | amount, size |
| Light Leak | `light_leak` | `tve.overlay.light_leak` | 1 | amount, hue, speed |
| VHS | `vhs` | `tve.overlay.vhs` | 1 | amount, jitter |
| Viewfinder | `viewfinder` | `tve.overlay.viewfinder` | 1 | opacity, color |

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

60 transitions, ids `tve.transition.<folder>`:

| Title | Folder | Categories | Params |
|---|---|---|---|
| Blinds | `blinds` | wipe | count, orientation, softness |
| Blur | `blur` | blur | radius |
| Card Flip | `card_flip` | 3d | axis |
| Carousel | `carousel` | 3d, slide | direction |
| Checkerboard | `checkerboard` | wipe, shape | squares |
| Chromatic | `chromatic` | stylize | — |
| Corner | `corner` | wipe, shape | softness |
| Cross Zoom | `cross_zoom` | zoom, blur | strength |
| Cube | `cube` | 3d | direction |
| Diamond | `diamond` | shape | softness |
| Dip to Black | `dip_to_black` | basic | color |
| Dip to White | `dip_to_white` | basic | color |
| Cross Dissolve | `dissolve` | basic | — |
| Door | `door` | 3d | — |
| Doorway | `doorway` | 3d | — |
| Dreamy | `dreamy` | distort | — |
| Fall | `fall` | 3d | — |
| Film Burn | `film_burn` | light | — |
| Flash | `flash` | basic, light | intensity |
| Fly Through | `fly_through` | 3d, zoom | — |
| Fold | `fold` | 3d | — |
| Glitch | `glitch` | stylize | strength |
| Heart | `heart` | shape | softness |
| Inset | `inset` | shape | softness |
| Iris | `iris` | shape | softness |
| Kaleidoscope | `kaleidoscope` | distort | segments |
| Random Bars | `luma_bars` | luma | softness |
| Blobs | `luma_blobs` | luma | softness |
| Burst | `luma_burst` | luma | softness |
| Clouds | `luma_clouds` | luma | softness |
| Hexagons | `luma_hex` | luma | softness |
| Fan | `luma_rays` | luma | softness |
| Rings | `luma_ripple` | luma | softness |
| Sand | `luma_sand` | luma | softness |
| Spiral | `luma_spiral` | luma | softness |
| Steps | `luma_steps` | luma | softness |
| Morph | `morph` | distort | strength |
| Page Turn | `page_turn` | 3d | direction |
| Pinch | `pinch` | distort | — |
| Pixelate | `pixelate` | stylize | blockSize |
| Polka Dots | `polka_dots` | shape | dots |
| Push | `push` | slide | direction |
| Clock Wipe | `radial_wipe` | wipe | softness |
| Reveal | `reveal` | slide | direction |
| Ripple | `ripple` | stylize, distort | amplitude, waves |
| Slide | `slide` | slide | direction |
| Spin | `spin` | zoom, 3d | turns |
| Split | `split` | wipe | orientation, softness |
| Squeeze | `squeeze` | slide | orientation |
| Star | `star` | shape | softness |
| Stretch | `stretch` | distort | — |
| Swing In | `swing_in` | 3d | direction |
| Swirl | `swirl` | distort | turns |
| Tiles | `tiles` | shape, 3d | — |
| Tumble | `tumble` | 3d | — |
| Water Drop | `water_drop` | distort | amplitude |
| Whip Pan | `whip` | slide, blur | direction |
| Wipe | `wipe` | wipe | direction, softness |
| Zigzag | `zigzag` | wipe | softness |
| Zoom In | `zoom_in` | zoom | strength |

## Overlays and looks

Packages whose `category` includes `"overlay"` are listed after the other effects and
marked "Overlay" in the library: procedural layers drawn over the picture (light leaks,
grain, dust and scratches, VHS, a viewfinder, bokeh), animated with `tve_time`.

`looks/looks.json` lists the Filters library: each look is a colour grade (the
`ColorGrade` JSON of a project file) and usually a 3D LUT in `looks/luts/`, both generated
by `tools/make_looks.py`. Built-in LUT paths start `:/effects/looks/luts/`.

## Audio effects

An audio effect package is a folder under `audio/` with a manifest of kind `audio-effect`:
no shaders, but a `chain` of built-in processors (`engine/audio/dsp/Processors.cpp`), each
with fixed `settings`. The package's params set the settings of the same name in every stage
that has them; a `mix` param blends the result with the dry sound. Params are numbers (float,
int, bool, enum). The engine has no Qt resources, so `engine/CMakeLists.txt` compiles the
manifests in as text; the library checks each against the processors (known processor, known
settings, every param used) and leaves out — with a reason — any that fail.

```json
{
  "id": "tve.audio.radio", "version": 1, "kind": "audio-effect", "category": ["voice"],
  "title": { "en": "Radio" },
  "params": [ { "id": "drive", "type": "float", "title": { "en": "Distortion" }, "min": 0, "max": 30, "default": 8 } ],
  "chain": [ { "processor": "eq", "settings": { "lowCut": 500, "highCut": 4000 } }, { "processor": "distortion" } ],
  "license": "TVE-Original"
}
```

| Processor | Settings |
|---|---|
| `eq` | b31 … b16k (dB, ten octave bands), lowCut, highCut (Hz, 24 dB/oct), gain |
| `compressor` | threshold, ratio, attack, release, knee, makeup |
| `limiter` | ceiling, release (2 ms look-ahead) |
| `gate` | threshold, ratio, range, attack, release (gate or expander) |
| `reverb` | size, damping, width, preDelay (output is the reverb alone: blend with `mix`) |
| `delay` | time, feedback, pingPong, tone, level |
| `chorus` | rate, depth, delay, feedback (also a flanger) |
| `widener` | width, haas |
| `autopan` | rate, depth |
| `pitch` | semitones, window |
| `distortion` | drive, tone, output |
| `ringmod` | frequency |
| `noise` | level, tone |
| `denoise` | amount, quiet (RNNoise 0.2 per channel; 30 ms late, so it blends its own dry sound: no `mix`) |

| Title | Folder | Category | Chain | Params |
|---|---|---|---|---|
| Bass Boost | `bass_boost` | eq | eq | b62 |
| Graphic EQ | `graphic_eq` | eq | eq | b31, b62, b125, b250, b500, b1k, b2k, b4k, b8k, b16k, gain |
| High Cut | `high_cut` | eq | eq | highCut |
| Low Cut | `low_cut` | eq | eq | lowCut |
| Treble Boost | `treble_boost` | eq | eq | b8k |
| Voice Clarity | `voice_clarity` | eq | eq | b4k |
| Warmth | `warmth` | eq | eq | b250 |
| Compressor | `compressor` | dynamics | compressor | threshold, ratio, attack, release, makeup |
| Expander | `expander` | dynamics | gate | threshold, ratio |
| Limiter | `limiter` | dynamics | limiter | ceiling, release |
| Noise Gate | `noise_gate` | dynamics | gate | threshold, range |
| Voice Leveler | `voice_leveler` | dynamics | compressor → limiter | makeup |
| Bathroom | `bathroom` | space | reverb | mix |
| Cave | `cave` | space | reverb | mix |
| Church | `church` | space | reverb | mix |
| Concert Hall | `hall` | space | reverb | mix |
| Corridor | `corridor` | space | reverb | mix |
| Large Room | `large_room` | space | reverb | mix |
| Reverb | `reverb` | space | reverb | size, damping, width, preDelay, mix |
| Small Room | `small_room` | space | reverb | mix |
| Stadium | `stadium` | space | reverb | mix |
| Echo | `echo` | echo | delay | time, feedback, level |
| Ping-Pong Echo | `ping_pong` | echo | delay | time, feedback, level |
| Slapback | `slapback` | echo | delay | time |
| Chorus | `chorus` | modulation | chorus | rate, depth |
| Flanger | `flanger` | modulation | chorus | rate, feedback |
| Rotating | `rotating` | modulation | autopan | rate, depth |
| Stereo Widener | `stereo_widener` | modulation | widener | width, haas |
| Pitch Shift | `pitch_shift` | pitch | pitch | semitones |
| Child | `child_voice` | voice | pitch → eq | semitones |
| Chipmunk | `chipmunk` | voice | pitch → eq | semitones |
| Deeper Voice | `deeper_voice` | voice | pitch → eq | semitones |
| Higher Voice | `higher_voice` | voice | pitch → eq | semitones |
| Megaphone | `megaphone` | voice | eq → distortion | drive |
| Monster | `monster` | voice | pitch → eq → distortion | semitones |
| Radio | `radio` | voice | eq → distortion → noise | drive, mix |
| Robot | `robot` | voice | ringmod → chorus → eq | frequency |
| Telephone | `telephone` | voice | eq → distortion | mix |
| Underwater | `underwater` | voice | eq → chorus → reverb | mix |
| Walkie-Talkie | `walkie_talkie` | voice | eq → distortion → noise | drive |
| Light Noise Reduction | `light_noise_reduction` | cleanup | denoise | amount |
| Noise Reduction | `noise_reduction` | cleanup | denoise | amount, quiet |
| Podcast Voice | `podcast_voice` | cleanup | denoise → eq → compressor → limiter | amount, makeup |
| Quiet Between Words | `quiet_between_words` | cleanup | denoise | quiet |
| Voice Cleanup | `voice_cleanup` | cleanup | denoise → eq → compressor | amount |

## Stickers

`stickers/` holds the built-in stickers: Lottie (Bodymovin JSON) animations of 512 × 512 at
30 fps in two-second loops, one file each, and `stickers.json`, the catalog the Stickers tab
reads (`{id, title, category, file}`; categories shapes, marks, nature, emoji, party). They
are generated, with original designs, by `tools/make_stickers.py`; edit that and run it again
rather than editing the files. The app draws them with ThorVG (`engine/decode/LottieAnimation`),
which reads shape layers (ellipses, rectangles, stars and polygons, paths), fills and strokes,
groups with transforms, and animated layer transforms.
