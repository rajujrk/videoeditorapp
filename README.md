# TechieVideoEditor

A desktop video editor for macOS (Apple Silicon first). See
[docs/TechieVideoEditor_Build_Plan.md](docs/TechieVideoEditor_Build_Plan.md) for the full plan.

**Status: Phase 1 (MVP editor) complete — slices 1–4 and 5a–5d (export, autosave and
crash recovery, proxies, voice-over). Phase 2: keyframes (6a), curves with effect keyframes (6b), speed ramps (6c) and colour grading (7a) done.** You can import video and audio files, arrange them on a multi-track timeline
(move, trim, split, delete with ripple on the main track, undo/redo), layer clips with
position, scale, rotation, crop, opacity and blend modes (animated with keyframes, as is
volume), grade each clip's colour (basic corrections, wheels, curves, LUTs), give each clip its own effects,
put any of 21 transitions on the cuts between clips (with a constant-power sound
crossfade), add titles (font, size, colour, bold/italic, alignment, letter and line
spacing, outline, shadow), change a clip's speed (0.1–10×) or ramp it, play it in reverse or insert
freeze frames, hear and see their sound (waveforms, volume, fades, detach audio, track
mute/hide/lock, meter), see thumbnails in the media bin and on the timeline, play and
scrub, save/open `.tve` projects (unsaved work is autosaved and recoverable after a crash),
edit heavy 4K media smoothly through proxies, record voice-overs from a microphone at the
playhead, and export MP4 files (H.264 or HEVC with AAC sound).

What's in the repo:

- `core/` — project model (tracks, clips, media, keyframes with easing), undoable edit commands (add, move, trim,
  split, delete/ripple delete), timeline queries, JSON project save/load. Plain C++20, no Qt.
- `engine/encode/` — `Mp4Writer`: H.264/HEVC through VideoToolbox plus AAC (or video
  only), muxed to a fast-start MP4 (LGPL FFmpeg encoders only); `makeProxy`: 720p H.264
  proxies with a keyframe every 10 frames, from 8- or 10-bit sources; `WavWriter`: float
  WAV files whose header stays current, so a take cut short by a crash still opens.
- `engine/` — FFmpeg video decoder (VideoToolbox, software fallback, frame-accurate and
  keyframe seek) and audio decoder (48 kHz stereo float, sample-accurate seek); timeline
  playback that stitches video across tracks and gaps, plays clips at any speed, in
  reverse (decoded a group of pictures at a time) or as freeze frames, and mixes all
  audible clips with volume, fades, speed and mute; miniaudio output slaved to the shared
  play/pause/seek clock; `AudioRecorder`: miniaudio microphone capture to WAV through a
  lock-free ring and a writer thread; waveform peak cache; thumbnail extraction.
- `effects/` — built-in effect packages (manifest + GLSL shaders): color adjust, gaussian
  blur, vignette, RGB split, pixelate; and 21 original transition packages (dissolve, dips,
  flash, push, slide, reveal, squeeze, wipes, iris, zooms, spin, pixelate, blur, ripple,
  door, …). See [effects/README.md](effects/README.md) for the
  manifest format and shader contract.
- `engine/gpu/` — effect library (manifests cross-checked against shader reflection), the
  GPU effect chain, and the compositor that places layers on the project canvas
  (fit, crop, scale, rotation, position, opacity, seven blend modes) on Qt RHI.
- `ui/player/` — `FrameCompositor` (zero-copy CVPixelBuffer → Metal textures, YUV → RGB
  per layer, per-clip effects, titles, transitions, compositor), shared by `VideoView`
  (plus output effects, aspect-fit, live preview of uncommitted slider changes) and the
  `Exporter`; `ProjectController`, which exposes the project, edits,
  transport and file I/O to QML, the title text renderer, and image providers for video
  thumbnails, transition previews and title previews.
- `ui/theme/` — design system: color/type/spacing tokens (dark and light), original icon
  set and base components. See [docs/design-system.md](docs/design-system.md).
- `ui/shell/` — Home screen and Editor workspace: media bin, player with transport,
  inspector (Video, Speed, Audio and Effects tabs, and a transition page), and the
  project-bound timeline.
- `app/` — the application window and command-line options.

### Editor shortcuts

| Key | Action |
|---|---|
| Space | Play / pause (stops a voice-over take) |
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

**Colour.** The inspector's Color tab grades the selected video clip; the grade runs on the
GPU before the clip's effects, in the preview and in exports. Basic: exposure (±4 stops),
contrast, saturation, temperature and tint. Wheels: lift (shadows), gamma (midtones), gain
(highlights) and offset — drag a wheel's puck towards a hue to push that way (double-click
centres it) and use the slider under it for all channels. Curves: master, red, green and
blue tone curves (drag points, double-click to add or remove one). LUT: choose a `.cube` or
`.3dl` 3D LUT and set its intensity (an unreadable file is refused; a LUT that has gone
missing is marked and left out). Copy and Paste move a grade between clips, Reset clears
it, and Presets save a grade by name (in
`~/Library/Application Support/TechieVideoEditor/Color Presets`; `TVE_COLOR_PRESET_DIR`
moves them) to apply to other clips. Sliders preview
live and each change is one undo step.

**Speed ramps.** In the Speed tab, pick a ramp (Ramp up, Ramp down, Flash in, Flash out,
Bullet time, Hero moment, Montage, Jumper) to change the speed through the clip. A ramp is
a set of points along the clip, each with a speed (0.1× to 10×), and the speed changes in
straight lines between them; the clip plays the same footage, so its length follows the
ramp's average speed and later clips move with its end. The graph shows the ramp on a
logarithmic scale with the playhead: drag a point up or down (and the inner ones sideways),
double-click to add one, select one to remove it, or Remove ramp to go back to a constant
speed (the average, same length). Splitting a ramped clip keeps every frame where it was;
Reverse keeps the ramp; picking a constant speed replaces it. Sound follows the ramp with
its pitch rising and falling like a tape (pitch-preserving stretching comes with the audio
effects); thumbnails and waveforms follow it too, and its clip is labelled `Ramp ·`.

**Proxies.** A proxy is a lighter copy of a video for the preview: 720p H.264 with a
keyframe every 10 frames (cheap to seek and play backwards), the same frames at the same
times, no sound. Videos larger than 1080p get one automatically on import (or opening a
project); any video can get one from its tile in the media bin (hover: Make proxy / Remove
proxy), which shows "Proxy 45%" while it is made and "Proxy" when ready. Proxies are made in
the background one at a time and kept in `~/Library/Caches/TechieVideoEditor/…/Proxies`
(`TVE_PROXY_DIR` moves it), named from the source's path, size and modification time, so
a changed source gets a new one; they are never saved in the project. The player's
proxy button switches between proxies and originals; sound, thumbnails and export always
use the originals. On an M-series Mac a 20 s 4K60 video's proxy takes about 10 s, and 4K60
played at 2× in reverse — beyond the hardware decoder with the original — holds 60 fps.
Scripted runs never start proxies on their own; `--make-proxies` makes them and quits,
`--no-proxies` plays the originals.

**Keyframes.** Position, scale, rotation, opacity, the crop edges and volume can be
animated. In the inspector's Video tab, Keyframes → Add keyframe keeps placement and
opacity as they are at the playhead; move the playhead and change a value, and the clip
animates between the two. The ◆ beside each value (and beside the Audio tab's gain)
animates just that one: filled when it has a keyframe at the playhead, outlined while it is
animated. While a value is animated, changing it sets its keyframe at the playhead (adding
one if needed). Step between keyframes with the arrows, choose how a keyframe eases to the
next (linear, ease in, ease out, ease in and out, hold), remove one, or Clear to stop
animating (each value keeps what it was at the playhead). For a custom timing, choose Custom curve
(or drag a handle in the curve editor that appears under the easing): it draws how the
value travels to the next keyframe, time left to right, progress bottom to top, and may
overshoot (Overshoot, Anticipate and Snap are one click; values stay inside their range).
Effects animate too: the ◆ beside an effect's slider in the Effects tab keys that parameter
(blur radius, pixelate size, colour adjustments, vignette, RGB split amount and angle), and
those keyframes travel with the effect when it is reordered or removed. Selected clips show their
keyframes as diamonds along the bottom: click one to go there, drag it to move it in time.
The timeline toolbar's ◆ adds or removes a keyframe for the selected clip. Keyframes stay
on the same moments when a clip is trimmed, split or sped up; a freeze frame holds the
placement of its moment. Preview, export and the sound mix all use them.

**Voice-over.** Click the microphone in the timeline toolbar to open the voice-over panel:
pick the microphone, check its level, and press the red button. After an optional
3-second count-in, recording starts at the playhead and the timeline plays along (on past
its end if you keep talking), muted by default so speakers aren't recorded too. Press the
button again, or Space, to stop: the take lands where it started, on the topmost audio
track with room for all of it, or on a new "Voice-over" track at the bottom, as one
undoable step ("Record Voice-over"), and is selected. Seeking is ignored while recording;
closing the panel or the project keeps the take. Takes are 48 kHz mono WAV files (played
at full level on both sides) in "<project> Voice-overs" beside a saved project, or
`~/Movies/TechieVideoEditor Voice-overs` before the first save; one device period of
input latency is trimmed from the start so the voice lines up with the picture. macOS asks
for microphone access the first time; if it was refused, the panel links to the privacy
setting. `TVE_AUDIO_INPUT=null` records silence without a microphone and
`TVE_RECORDING_DIR` moves the folder (the tests use both).

**Autosave and recovery.** Unsaved changes are written to
`~/Library/Application Support/TechieVideoEditor/Autosave` two seconds after edits settle
(at least every 30 s while they keep coming); the top bar shows "Autosaved hh:mm". The copy
is deleted once the work is safe: saved, undone back to the saved state, or discarded. It
is kept when the app crashes, quits with unsaved changes, or switches to another project,
and Home then lists it under "Unsaved work": Recover opens it as an unsaved project that
still saves to its original file (if it had one); Discard deletes it. Work still open in
another running copy of the app is not listed. Project files themselves are written to a
temporary file and renamed, so a crash mid-save never leaves a half-written project.
`TVE_AUTOSAVE_DIR` moves the folder (the tests use a scratch one); scripted runs
(`--stats`, `--quit-after`, `--screenshot`, `--export`) do not autosave.

**Export.** Click Export (⌘E) to save an MP4: choose the file, the size (the project's,
or 4K/1440p/1080p/720p/480p smaller than it, same shape), H.264 (plays everywhere) or HEVC
(smaller files), and quality (High, Standard, Small; the dialog estimates the file size).
Export renders every frame at the project's frame rate with the same engine as the preview
(transitions, titles, effects, sound) on an offscreen GPU device, converts to NV12 on the
GPU, and encodes with VideoToolbox; you can keep editing while it runs (it uses a snapshot)
or cancel it, which removes the partial file. From the command line:

```bash
build/macos-release/app/TechieVideoEditor.app/Contents/MacOS/TechieVideoEditor project.tve --export out.mp4 --export-size 1920x1080 --export-codec h264
```

On an M-series Mac, 20 s of 4K60 exports in about 17 s to 4K H.264, 8 s to 1080p and 25 s
to 4K HEVC.

**Titles.** Click a style in the library's Titles tab to add a 5-second title at the
playhead (on the lowest free video track above the main one, or a new track on top), or
drag it onto a track. Titles are clips like any other: move, trim (to any length), split,
place and fade them with the Video tab, add effects. The Video tab's Text section edits
the words (committed when typing pauses), font, size, bold/italic, alignment, colour,
letter and line spacing, outline and shadow. Sizes are fractions of the canvas, so a title
looks the same at any resolution. Titles are drawn by Qt's text engine (HarfBuzz shaping)
to a canvas-sized texture, redrawn only when they change.

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
clip's decoder opens can dip to the low 50s), and with titles over it; 4K30
plays at 2× in reverse at 30 fps. 4K60 at 2× in reverse needs about 120 hardware decodes
a second, the decoder's ceiling, so with the original it can drop frames under load; with
its proxy it holds 60 fps. `tve_engine_tests "[perf]"` runs the playback throughput benchmarks.

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
