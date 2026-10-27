# TechieVideoEditor

A desktop video editor for macOS (Apple Silicon first). See
[docs/TechieVideoEditor_Build_Plan.md](docs/TechieVideoEditor_Build_Plan.md) for the full plan.

**Status: Phase 1 (MVP editor), slices 1–2 of 5 done.** You can import video and audio
files, arrange them on a multi-track timeline (move, trim, split, delete with ripple on
the main track, undo/redo), hear and see their sound (waveforms, per-clip volume and
fades, detach audio, track mute/hide/lock, level meter), play and scrub the timeline,
apply GPU effects to the output, and save/open `.tve` projects. Not yet: transitions,
titles, clip transforms, voice-over, export.

What's in the repo:

- `core/` — project model (tracks, clips, media), undoable edit commands (add, move, trim,
  split, delete/ripple delete), timeline queries, JSON project save/load. Plain C++20, no Qt.
- `engine/` — FFmpeg video decoder (VideoToolbox, software fallback, frame-accurate seek)
  and audio decoder (48 kHz stereo float, sample-accurate seek); timeline playback that
  stitches video across tracks and gaps and mixes all audible clips with volume, fades and
  mute; miniaudio output slaved to the shared play/pause/seek clock; waveform peak cache.
- `effects/` — built-in effect packages (manifest + GLSL shaders): color adjust, gaussian
  blur, vignette, RGB split, pixelate. See [effects/README.md](effects/README.md) for the
  manifest format and shader contract.
- `engine/gpu/` — effect library (manifests cross-checked against shader reflection) and
  the GPU effect chain on Qt RHI.
- `ui/player/` — `VideoView` (zero-copy CVPixelBuffer → Metal textures, YUV → RGB, effect
  chain, aspect-fit) and `ProjectController`, which exposes the project, edits, transport
  and file I/O to QML.
- `ui/theme/` — design system: color/type/spacing tokens (dark and light), original icon
  set and base components. See [docs/design-system.md](docs/design-system.md).
- `ui/shell/` — Home screen and Editor workspace: media bin, player with transport,
  inspector (effects live; other tabs planned), and the project-bound timeline.
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

Drag a clip to move it (it snaps to clip edges and the playhead and can change tracks);
drag its left or right edge to trim; drag the round knobs at its top corners to set fade
lengths; click or drag the ruler to scrub. The inspector's Audio tab sets volume and fades
and detaches a video clip's sound to an audio track.

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

`--effects` takes `all` or comma-separated effect ids; `--screenshot` saves the window
after `--screenshot-at` seconds (default 3); `--screen home|editor` picks the start
screen; `--theme dark|light` picks the theme; `--play` starts playback; `--seek <s>` moves the
playhead.

## Rules

- Never copy files from another product's app bundle into this repo (effects, LUTs, presets,
  models, fonts, sounds, templates). See §1 of the build plan.
- FFmpeg must stay an LGPL build; `tve_engine_tests` fails if a GPL build is linked.
