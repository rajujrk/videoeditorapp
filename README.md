# TechieVideoEditor

A desktop video editor for macOS (Apple Silicon first). See
[docs/TechieVideoEditor_Build_Plan.md](docs/TechieVideoEditor_Build_Plan.md) for the full plan.

**Status: Phase 1 (MVP editor), slices 1, 2, 3a, 3b and 4a done.** You can import video
and audio files, arrange them on a multi-track timeline (move, trim, split, delete with
ripple on the main track, undo/redo), layer clips with position, scale, rotation, crop,
opacity and blend modes, give each clip its own effects, put any of 21 transitions on the
cuts between clips (with a constant-power sound crossfade), change a clip's speed
(0.1–10×), play it in reverse or insert freeze frames, hear and see their sound (waveforms,
volume, fades, detach audio, track mute/hide/lock, meter), see thumbnails in the media bin
and on the timeline, play and scrub, and save/open `.tve` projects. Not yet: titles,
voice-over, export.

What's in the repo:

- `core/` — project model (tracks, clips, media), undoable edit commands (add, move, trim,
  split, delete/ripple delete), timeline queries, JSON project save/load. Plain C++20, no Qt.
- `engine/` — FFmpeg video decoder (VideoToolbox, software fallback, frame-accurate and
  keyframe seek) and audio decoder (48 kHz stereo float, sample-accurate seek); timeline
  playback that stitches video across tracks and gaps, plays clips at any speed, in
  reverse (decoded a group of pictures at a time) or as freeze frames, and mixes all
  audible clips with volume, fades, speed and mute; miniaudio output slaved to the shared
  play/pause/seek clock; waveform peak cache; thumbnail extraction.
- `effects/` — built-in effect packages (manifest + GLSL shaders): color adjust, gaussian
  blur, vignette, RGB split, pixelate; and 21 original transition packages (dissolve, dips,
  flash, push, slide, reveal, squeeze, wipes, iris, zooms, spin, pixelate, blur, ripple,
  door, …). See [effects/README.md](effects/README.md) for the
  manifest format and shader contract.
- `engine/gpu/` — effect library (manifests cross-checked against shader reflection), the
  GPU effect chain, and the compositor that places layers on the project canvas
  (fit, crop, scale, rotation, position, opacity, seven blend modes) on Qt RHI.
- `ui/player/` — `VideoView` (zero-copy CVPixelBuffer → Metal textures, YUV → RGB per
  layer, per-clip effects, compositor, output effects, aspect-fit, live preview of
  uncommitted slider changes), `ProjectController`, which exposes the project, edits,
  transport and file I/O to QML, and `ThumbnailProvider` (`image://tvethumb/…`, decoded on
  a small thread pool and cached).
- `ui/theme/` — design system: color/type/spacing tokens (dark and light), original icon
  set and base components. See [docs/design-system.md](docs/design-system.md).
- `ui/shell/` — Home screen and Editor workspace: media bin, player with transport,
  inspector (Video, Speed, Audio and Effects tabs, and a transition page), and the
  project-bound timeline.
- `app/` — the application window and command-line options.

### Editor shortcuts

| Key | Action |
|---|---|
| Space | Play / pause |
| ← / → | Previous / next frame |
| Home / End | Go to start / end |
| S | Split the selected clip (or the visible clip) at the playhead |
| ⌫ / Delete | Delete the selected clip (closes the gap on the main track) |
| ⌘Z / ⇧⌘Z | Undo / redo |
| ⌘S | Save project |
| ⌘I | Import media |

Clips on higher video tracks are drawn over lower ones. Select a clip and use the
inspector's Video tab for position, scale, rotation, crop, opacity and blend mode, and its
Effects tab (or click an effect in the library) for the clip's effect stack. Sliders
preview live and commit one undo step on release. The first clip added to an untouched
project sets the canvas size and frame rate; a canvas picked on Home is kept.

Drag a clip to move it (it snaps to clip edges and the playhead and can change tracks);
drag its left or right edge to trim; drag the round knobs at its top corners to set fade
lengths; click or drag the ruler to scrub. The inspector's Audio tab sets volume and fades
and detaches a video clip's sound to an audio track.

The inspector's Speed tab sets a clip's speed (slider from 0.1× to 10×, or 0.5/1/2/4×
presets) and direction; the clip gets shorter or longer and later clips on its track move
with it. Reversed clips play their sound backwards too. "Freeze frame at playhead" splits
the clip there and inserts a 2-second still (silent; trim it to change its length). Clip
labels show `2× ·`, `Reverse ·` or `Still ·` badges.

**Transitions.** Click one in the library's Transitions tab to put it on the cut nearest
the playhead (on the selected clip's track, else the main track), or drag it onto a cut.
It shows as a marker over the cut; click the marker to edit it in the inspector: kind,
duration, position (before, centred on or after the cut) and its own settings. ⌫ removes
the selected transition. A transition plays the media beyond each clip's edges (its
"handles"); where the media runs out, the edge frame is held. Each side of a transition is
limited to half its clip, so neighbouring transitions never overlap. Moving or deleting a
clip removes the transitions it was part of; trimming keeps them, and they play again
whenever the clips touch.

On an M-series Mac a 4K60 H.264 clip plays at 60 fps forwards, in reverse and at 2×, and
through any transition between two 4K60 clips (about 59–60 fps; the second the incoming
clip's decoder opens can dip to the low 50s); 4K30
plays at 2× in reverse at 30 fps. 4K60 at 2× in reverse needs about 120 hardware decodes
a second, the decoder's ceiling, so it can drop frames under load; proxies (slice 5) are
the answer there. `tve_engine_tests "[perf]"` runs the playback throughput benchmarks.

Set `TVE_AUDIO=null` to render audio without a sound card (used by the tests), or
`TVE_AUDIO=none` to disable audio output.

## Requirements

- macOS 13+ on Apple Silicon, Xcode Command Line Tools
- CMake ≥ 3.25, Ninja, pkg-config, Qt 6.7+ (`brew install cmake ninja pkg-config qt`)
- vcpkg (`git clone https://github.com/microsoft/vcpkg ~/vcpkg && ~/vcpkg/bootstrap-vcpkg.sh`)

vcpkg builds FFmpeg (LGPL, with VideoToolbox), Catch2, nlohmann-json and miniaudio from `vcpkg.json`.
Qt comes from Homebrew because building it through vcpkg takes hours.

## Build and test

```bash
export VCPKG_ROOT=~/vcpkg
cmake --preset macos-release -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build --preset macos-release
tools/make_test_media.sh          # synthetic test videos in samples/
ctest --preset macos-release
```

## Run

```bash
build/macos-release/app/TechieVideoEditor.app/Contents/MacOS/TechieVideoEditor
```

Media files given on the command line are imported and appended to the main track; a
`.tve` file is opened. For an unattended measurement:

```bash
build/macos-release/app/TechieVideoEditor.app/Contents/MacOS/TechieVideoEditor --stats --quit-after 15 samples/4k60_h264.mp4
build/macos-release/app/TechieVideoEditor.app/Contents/MacOS/TechieVideoEditor --effects all --screenshot shot.png samples/4k60_h264.mp4
build/macos-release/tools/tve_decode_bench samples/4k60_h264.mp4
```

`--effects` applies output effects to the whole canvas (all, or comma-separated ids); `--screenshot` saves the window
after `--screenshot-at` seconds (default 3); `--screen home|editor` picks the start
screen; `--theme dark|light` picks the theme; `--play` starts playback; `--seek <s>` moves the
playhead.

## Rules

- Never copy files from another product's app bundle into this repo (effects, LUTs, presets,
  models, fonts, sounds, templates). See §1 of the build plan.
- FFmpeg must stay an LGPL build; `tve_engine_tests` fails if a GPL build is linked.
