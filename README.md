# TechieVideoEditor

A desktop video editor for macOS (Apple Silicon first). See
[docs/TechieVideoEditor_Build_Plan.md](docs/TechieVideoEditor_Build_Plan.md) for the full plan.

**Status: Phase 1 (MVP editor) complete — slices 1–4 and 5a–5e (export, autosave and
crash recovery, proxies, voice-over, still images). Phase 2: keyframes (6a), curves with effect keyframes (6b), speed ramps (6c), colour grading (7a), HSL, hue curves and scopes (7b), looks, overlays and adjustment layers (7c), masks, keying and split screens (8a), the effects library (8b), audio effects, mixing and loudness (9a), the titles system (9b), 60 transitions (10a), stickers (10b) and markers, groups and compound clips (11a) done.** You can import video, audio and image files, arrange them on a multi-track timeline
(move, trim, split, delete with ripple on the main track, undo/redo), layer clips with
position, scale, rotation, crop, opacity and blend modes (animated with keyframes, as is
volume), grade each clip's colour (basic corrections, wheels, curves, HSL, hue curves, LUTs) with
scopes, put looks (filters) and overlays on clips or on adjustment layers above them, cut clips out with shape and pen masks, a chroma key or a track matte, arrange clips side by side or picture in picture, give each clip its own effects from a library of 61 (with shadows, outlines and fills that reach outside the picture),
put any of 60 transitions on the cuts between clips (one on every cut at once, or a random one each) (with a constant-power sound
crossfade), add titles (font, size, colour, bold/italic, alignment, letter and line
spacing, outline, shadow, a shape or bubble behind them, in/out/loop text animations by
letter, word or line, rolling credits and tickers; 40 templates), change a clip's speed (0.1–10×) or ramp it, play it in reverse or insert
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
- `effects/` — built-in packages, all original (manifest + GLSL shaders): 61 video effects,
  60 transitions (basic, slides, wipes, shapes, zooms, 3D, distortions, luma wipes, light) and
  40 audio effects (manifest chains, no shaders). See [effects/README.md](effects/README.md) for the
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
| M | Add a marker at the playhead |
| ⌥← / ⌥→ | Previous / next marker |
| ⇧-click / ⌘-click | Add a clip to the selection, or take it out |
| ⌘G / ⇧⌘G | Group the selected clips / ungroup |
| ⌥⌘G | Make the selected clips a compound clip, or break the selected one apart |

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
missing is marked and left out). HSL: pick one of eight hue bands (red, orange, yellow, green, aqua, blue,
purple, magenta) and turn its hue (±30°), saturation and luminance; colours between two
bands take a blend of both, greys are left alone. Hue curves: saturation by hue, hue by hue
(±60°) and saturation by brightness, each flat at the middle line (on hue curves the two
ends are the same red and move together). Scopes, at the top of the tab, measure the
preview as it is on screen: waveform, RGB parade, vectorscope (with 75 % colour targets and
the skin-tone line) and histogram; the preview sends a small copy of its frames only while
a scope is showing. Copy and Paste move a grade between clips, Reset clears
it, and Presets save a grade by name (in
`~/Library/Application Support/TechieVideoEditor/Color Presets`; `TVE_COLOR_PRESET_DIR`
moves them) to apply to other clips. Sliders preview
live and each change is one undo step.

**Images.** Photos and graphics import like video: PNG, JPEG, HEIC/HEIF, WebP, TIFF and BMP
(and a GIF of one frame), from the Import button (⌘I) or by dropping them on the editor. A
photo taken on its side is turned upright by its EXIF tag. The media bin marks them
"Image"; on the timeline an image becomes a 5-second clip you can trim to any length. It
has no speed, but everything else applies — placement and crop, keyframes (a slow push in or
pan), colour, looks, effects, transitions, export — and transparency in PNGs shows what is
below. An animated GIF plays as a video. Images do not set the project's canvas; the first
video does.

**Stickers and transparent video.** The library's Stickers tab holds 100 original animated
stickers — 25 designs (star, heart, sparkle, burst, gem, highlight ring, arrows, check, cross,
exclamation and question marks, map pin, swoosh, sun, cloud, raindrop, flame, lightning,
smiley, surprised face, speech bubble, music note, crown, confetti), each pulsing, spinning,
bouncing or wobbling in a two-second loop — with category chips, the search, and previews that
play on hover. Click one to put it at the playhead on a free track above the main one, small and
in the middle; it loops for as long as its clip lasts and is placed, graded, masked and given
effects like any picture. Stickers are Lottie animations drawn by ThorVG (MIT); Lottie files of
your own (.json) import the same way. Video with transparency — ProRes 4444, GIF, animated PNG
or WebP — now keeps it, so it plays over what is below (it decodes in software, and gets no
proxy). The stickers are generated by `tools/make_stickers.py` into `effects/stickers/`.

**Markers, groups and compound clips.** Press M (or the flag button) to drop a marker at the
playhead; it shows as a coloured flag on the ruler with a faint line down the lanes, clips snap
to it, and ⌥← / ⌥→ jump between markers. Click a flag to give it a note, one of six colours,
a length (a range, drawn as a band) or to make it a chapter; drag it to move it. Chapter
markers become the chapters of an exported MP4 (titled by their notes; an "Opening" chapter
covers any time before the first). ⇧- or ⌘-click clips to select several; ⌘G groups them, so
clicking one selects all, dragging one moves all (each on its own track) and deleting one
deletes all; ⇧⌘G ungroups. ⌥⌘G (or the stack button) puts the selected clips, from any tracks,
into a compound clip: one clip on the lowest of their tracks that plays them as they were
(layers, placement, effects, sound and track volumes) and is moved, trimmed, split, placed,
graded, given effects, faded and mixed like any clip (it is marked with the stack icon). It has no speed or transitions yet.
⌥⌘G on a compound clip breaks it apart, putting the part it plays back on the timeline.

**60 transitions.** The Transitions tab now holds 60, filtered by category chips (basic, slide,
wipe, shape, zoom, 3D, distort, luma, stylize, blur, light) and the search: besides the first
21, ten in 3D (Cube, Card Flip, Page Turn, Fold, Swing In, Fall, Tumble, Doorway, Fly Through,
Carousel), ten distortions and effects (Swirl, Water Drop, Morph, Dreamy, Pinch, Stretch,
Kaleidoscope, Glitch, Chromatic, Whip Pan), ten luma wipes revealing the incoming picture by a
pattern (Clouds, Spiral, Burst, Random Bars, Blobs, Rings, Steps, Fan, Hexagons, Sand), shapes
growing from the centre or a corner (Heart, Star, Diamond, Inset, Corner, Zigzag, Polka Dots,
Tiles) and Film Burn. A selected transition's **Apply to all cuts** puts it, at its length, on
every cut between clips on the unlocked video tracks; **Random on every cut** in the tab gives
each cut a different random one. Either is one undo step.

**Titles system.** The library's Titles tab holds 40 original templates in five categories
— titles, lower thirds, callouts (speech and thought bubbles, an arrow, a badge, a tag),
credits (rolling credits, a scrolling quote, a news ticker, end and chapter cards) and
animated — with previews drawn by the app's own text renderer that play on hover. A title can
have a shape behind its text (box, rounded box, pill, speech or thought bubble with a movable
tail, banner, underline, circle, arrow or tag, any colour and opacity, with padding), and
three animations: **In** from the clip's start and **Out** ending at its end — fade, rise,
drop, slide left or right, zoom, pop, spin, flip, bounce, typewriter, focus, wipe or tracking
— and a **Loop** while it shows — wave, pulse, flicker, shake or swing. Each moves the whole
text, its lines, words or letters, one after another by a stagger, for a duration; the shape
arrives and leaves with the text. **Scroll** rolls the text up the frame over the whole clip
(credits) or crawls it from right to left (a ticker). Everything is drawn per letter, so it
looks the same in the player and the export.

**Audio effects, mixer and loudness.** The Audio tab's Audio effects section stacks 40 effects
of our own on a clip's sound: EQ (Graphic EQ, Bass Boost, Treble Boost, Voice Clarity, Warmth,
Low Cut, High Cut), dynamics (Compressor, Limiter, Noise Gate, Expander, Voice Leveler), rooms
(Reverb, Small Room, Large Room, Concert Hall, Church, Cave, Corridor, Bathroom, Stadium),
echoes (Echo, Ping-Pong Echo, Slapback), modulation (Chorus, Flanger, Stereo Widener,
Rotating), Pitch Shift, and voices (Deeper Voice, Higher Voice, Child, Chipmunk, Monster,
Robot, Radio, Telephone, Walkie-Talkie, Underwater, Megaphone). They run after the clip's
volume and fades, and echoes and reverb ring on past its end. Each is a package of data
(`effects/audio`) chaining built-in processors — a ten-band EQ with 24 dB/octave cuts, a
compressor, a look-ahead limiter, a gate, an algorithmic reverb, delays, chorus, a widener,
auto-pan, a granular pitch shifter, distortion, a ring modulator and noise. Played faster or
slower, a clip now keeps its pitch (time-stretched; the Speed tab's **Keep pitch** switch
turns it back into tape-style). **Normalize** in the Loudness section measures a clip
(ITU-R BS.1770) and sets its volume to −14, −16 or −23 LUFS, and the export dialog can
normalize the whole mix the same way, with a limiter at −1 dBFS. The timeline's **Mixer**
button opens a strip per track — volume, balance, mute, solo and a peak/RMS meter — and the
master with momentary, short-term and integrated loudness and its peak.

**Effects library.** The library's Effects tab holds 61 original effects with previews
rendered on the GPU (they animate on hover), filtered by category chips — Blur (Gaussian Blur,
Directional Blur, Radial Blur, Spin Blur, Lens Blur, Tilt Shift, Frosted Glass, Smooth, Sharpen),
Colour (Black & White, Duotone, Tint, Posterize, Solarize, Threshold, Colour Splash, Gradient
Map, Hue Rotate, Color Adjust), Stylize (Find Edges, Pencil Sketch, Cartoon, Emboss, Halftone, Oil Paint,
Crystallize, LED Board, CRT Screen, Neon Edges, Pixelate, Vignette), Distort (Swirl, Bulge &
Pinch, Wave, Water Ripple, Mirror, Kaleidoscope, Little Planet, Camera Shake, Lens Distortion,
Heat Haze, Flip), Glitch (Digital Glitch, Bad Signal, RGB Split), Light (Glow, Light Rays, Lens
Flare, Strobe, Spotlight), Frame (Drop Shadow, Outline, Outer Glow, Background Fill, Rounded
Corners) and the six overlays — and by the search field (which also searches transitions).
Click one to add it to the selected clip; its settings are in the Effects tab, and the
numeric ones keyframe. Frame effects work on the placed clip, so a picture-in-picture clip
casts its shadow onto what is below and Background Fill covers the frame around a clip with
its own blurred picture or a colour. Effect sizes are relative to the picture, so previews,
proxies and exports match. See [effects/README.md](effects/README.md) for writing one.

**Masks, keying and split screens.** The Video tab's Masks section adds rectangle, ellipse,
line, star, heart and pen masks to a clip (up to eight; together they show the union of
their insides). The selected mask is drawn on the player with handles: drag the centre to
move it, the square to size it, the knob above it to rotate it; a pen mask's points drag one
by one, a click on the player adds a point on the nearest edge and a double-click removes
one. Sliders set the centre, size, rotation, corner roundness and feather, and Invert keeps
the outside instead. **Chroma key** removes a colour — Green, Blue, or Pick and click the
player — with tolerance, edge softness and spill removal (the key's colour cast is taken out
of what stays). **Track matte** shapes a clip by the clip on the track just above it, by
alpha or luma (each also inverted); the matte clip itself is not shown. **Split screen**
arranges the clips at the playhead, top track first: picture in picture, side by side, top
and bottom, three columns, one and two, or a grid of four, each clip cropped to fill its
part; one undo step puts them all back. Masks and keys are applied in the clip's own
picture, after its grade and before its effects, so they move, scale and animate with it,
and export renders them exactly as the player does.
**Looks, overlays and adjustment layers.** The library's Filters tab holds 12 looks of our
own — Warm Film, Teal & Orange, Bleach Bypass, Golden Hour, Cool Night, Vintage Fade, Matte
Pastel, Cross Process, Sepia, Noir, Vivid and Black & White — each previewed on a sample
scene. Click one to put it on the selected clip; the Color tab's Look section sets its
intensity or removes it. A look applies before the clip's own grade, so the two combine. The
ten LUTs behind the looks are also offered on their own under LUT → Built-in. Six overlays —
Light Leak, Film Grain, Dust & Scratches, VHS, Viewfinder and Bokeh — are animated effects
in the Effects tab (marked "Overlay") with their own settings, keyframable like any effect.
The first Filters tile adds an **adjustment layer** at the playhead: a clip with no picture
of its own whose look, grade and effects change everything below it while it lasts, blended
by its opacity (keyframe the opacity to fade it in or out). The looks and their LUTs are
generated by `tools/make_looks.py` into `effects/looks/`.

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
(transitions, titles, effects, compound clips, sound), writes chapter markers as chapters, on an offscreen GPU device, converts to NV12 on the
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

vcpkg builds FFmpeg (LGPL, with VideoToolbox), Catch2, nlohmann-json, miniaudio and ThorVG (MIT, for Lottie stickers) from `vcpkg.json`.
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
