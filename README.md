# TechieVideoEditor

A desktop video editor for macOS (Apple Silicon first). See
[docs/TechieVideoEditor_Build_Plan.md](docs/TechieVideoEditor_Build_Plan.md) for the full plan.

# Project Status and Feature Overview

## 1. Development Status

**Phase 1 – MVP Editor: Complete**

- Slices 1–4: Core MVP editor functionality.
- Slices 5a–5e:
  - Export
  - Autosave and crash recovery
  - Proxy media support
  - Voice-over recording
  - Still images

**Phase 2 – Advanced Editing Features: Complete**

- Slice 6a: Keyframes.
- Slice 6b: Curves with effect keyframes.
- Slice 6c: Speed ramps.
- Slice 7a: Colour grading.
- Slice 7b: HSL, hue curves and scopes.
- Slice 7c: Looks, overlays and adjustment layers.
- Slice 8a: Masks, keying and split screens.
- Slice 8b: Effects library.
- Slice 9a: Audio effects, mixing and loudness.
- Slice 9b: Titles system.
- Slice 10a: 60 transitions.
- Slice 10b: Stickers.
- Slice 11a: Markers, groups and compound clips.
- Slice 11b: Delivery features, including export formats, presets, ranges and render queue.
- Slice 11c: Hardening, including missing-media relinking, shortcut editor and a 531-case golden-frame test suite.

**Phase 3 – AI-Powered Editing and Advanced Media Processing: Complete**

- Slice 12a: Analysis features, including pauses, scene changes, beat detection and audio ducking.
- Slice 12b: Speech-to-text with captions and SRT/VTT support.
- Slice 12c: Caption styles and transcript-based editing.
- Slice 13a: Audio AI, including speech enhancement and noise reduction.
- Slice 13b: People-related features, including portrait cutout, face covering and automatic reframing.
- Slice 13c: Motion processing, including stabilization, flicker removal, temporal denoising and tracking.
- Slice 14a: Advanced vision features, including multicam synchronization by sound, smart cutout, upscaling, smooth slow motion and object removal.

**Phase 4 – Recording, Generative AI and Creative Tools: Complete**

- Slice 15a: Screen and camera recording with cursor effects.
- Slice 15b: Music and sound library, with imports from Photos, Music and connected devices.
- Slice 16a: AI writing, caption translation and AI voices using user-provided API keys.
- Slice 16b: AI-generated pictures, stickers, videos, music and sound effects.
- Slice 17a: Templates in six aspect ratios.
- Slice 17b: Instant videos, highlight montages and smart shorts.
- Slice 18a: Drawing, paths and overlay transitions.
- Slice 18b: Asset and template pack store.

**Phase 5 – Professional Editing and Extensibility: Complete**

- Slice 19a: Audio plug-ins supporting Audio Units and VST3.
- Slice 19b: Video plug-ins supporting OpenFX.
- Slice 20a: Colour management with HDR support, including PQ and HLG, an HDR viewer and 10-bit HEVC export.
- Slice 20b: 360° video support, including reframing, navigation and 360° export.
- Slice 21a: Project interchange supporting OpenTimelineIO and Final Cut Pro XML import/export, plus EDL export.
- Slice 21b: AI copilot supporting natural-language editing through Claude, with every proposed change displayed for review before application.
- Slice 22a: Voice cloning with consent.
- Slice 22b: Application localization in English and Tamil.
- Slice 22c: Performance hardening, including render files for resource-intensive sections, half-resolution previews and measured performance budgets.

**Additional Enhancements – Complete**

The following improvements were completed after Phase 5:

- Slice 23a: Speed and transition support for compound clips.
- Slice 23b: Transparent video export using WebM VP9 and HEVC.
- Slice 23c: MP3 audio export.
- Slice 24a: Right-click menus on clips, tracks, empty timeline space and media; adding, renaming and deleting tracks; cut, copy, paste, duplicate and ripple delete with shortcuts; the home screen's Record screen and Instant mode.
- Slice 24b: Drag and drop from Finder onto the timeline and player, and moving, resizing and rotating the selected clip by hand in the player.
- Slice 24c: Timeline tools — J K L shuttle, typing a timecode, Mark In/Out, the blade tool, closing gaps, track height, pasting attributes and transitions, crossfades on sound tracks, and speeds from 0.01× to 100×.
- Slice 24d: A source monitor (In and Out, insert, overwrite, append), compound clips opened in timeline tabs, projects imported as compound clips, and multicam clips with angle switching.
- Slice 25a: The anchor point, corner pin, clip animation presets, pan and zoom, colour clips, and snapshots of the player.
- Slice 25b: Motion blur that follows keyframed movement, effect and colour masks, and player guides, safe areas, rulers and zoom.
- Slice 25c: One-click auto colour and colour match, HDR colour wheels, and lens-correction presets for wide cameras.

All previously identified feature gaps have been closed.

---

## 2. Implemented Application Capabilities

### 2.1 Media Import and Timeline Editing

- Import video, audio and image files.
- Arrange media on a multi-track timeline.
- Move, trim, split and delete clips.
- Support ripple deletion on the main track.
- Undo and redo editing operations.
- Layer clips with support for position, scale, rotation, crop, opacity and blend modes.
- Animate clip properties using keyframes, including volume.

### 2.2 Colour Grading and Visual Adjustments

- Apply individual colour grading to each clip.
- Support basic colour corrections, colour wheels, curves, HSL adjustments and hue curves.
- Apply LUTs (Look-Up Tables).
- Display colour scopes for accurate grading.
- Apply looks (filters) and overlays to individual clips.
- Add adjustment layers above clips to apply effects across multiple clips.

### 2.3 Masks, Keying and Compositing

- Cut out clips using shape masks and pen-based masks.
- Support chroma keying and track mattes.
- Arrange clips side by side.
- Create picture-in-picture compositions.

### 2.4 Effects Library

- Access a library of 61 visual effects.
- Apply individual effects to each clip.
- Support shadows, outlines and fills that extend beyond the original picture boundaries.

### 2.5 Transitions

- Apply any of the 60 available transitions between clips.
- Apply a selected transition to every cut simultaneously.
- Randomly assign transitions across cuts.
- Support constant-power audio crossfades.

### 2.6 Titles and Text

- Customize fonts, sizes, colours, bold and italic styles.
- Configure text alignment, letter spacing and line spacing.
- Add outlines and shadows.
- Add shapes or speech bubbles behind text.
- Apply entrance, exit and looping text animations.
- Animate text by individual letters, words or lines.
- Create rolling credits and tickers.
- Choose from 40 built-in title templates.

### 2.7 Speed and Playback Controls

- Adjust clip playback speed from 0.01× to 100×.
- Create speed ramps.
- Play clips in reverse.
- Insert freeze frames.

### 2.8 Audio Editing and Mixing

- Display audio waveforms.
- Adjust clip volume and audio fades.
- Detach audio from video clips.
- Mute, hide and lock individual tracks.
- Display audio level meters.
- Record voice-overs directly from a microphone at the current playhead position.

### 2.9 Media Management and Preview

- Display thumbnails in the media bin and timeline.
- Play and scrub through video content.
- Edit heavy 4K media smoothly using proxy files.
- Save and reopen `.tve` project files.
- Automatically save unsaved work.
- Recover projects after unexpected application crashes.

### 2.10 Export and Delivery

- Export videos in MP4 format.
- Support H.264 and HEVC video encoding.
- Support AAC audio encoding.

---

## 3. Overall Completion Summary

The application has completed all five planned development phases, covering the MVP editor, professional editing capabilities, AI-powered media processing, generative AI tools, advanced integrations and performance optimization.

Additional enhancements have also closed the previously identified feature gaps.

The application now provides a comprehensive video editing environment with multi-track editing, professional colour grading, visual effects, transitions, audio processing, AI-assisted editing, media generation, advanced compositing, project interchange and export capabilities.
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
  45 audio effects (manifest chains, no shaders). See [effects/README.md](effects/README.md) for the
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
| ⌘E | Export (or add to the render queue) |
| ⌘/ | Keyboard shortcuts (change any of these) |
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

The inspector's Speed tab sets a clip's speed (slider from 0.01× to 100×, or 0.5/1/2/4×
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
your own (.json) import the same way. Video with transparency — ProRes 4444, WebM VP9 with
alpha, Apple's HEVC with alpha, GIF, animated PNG or WebP — now keeps it, so it plays over what is below (it decodes in software, and gets no
proxy). The stickers are generated by `tools/make_stickers.py` into `effects/stickers/`.

**Captions.** The library's Captions tab turns the speech in your clips into captions, on this
Mac (nothing is uploaded). Choose a Whisper model — Base (57 MB), Small (190 MB) or Large v3
Turbo (574 MB), English-only or 99 languages — and download it once (it is checked against its
SHA-256 and kept in `~/Library/Application Support/TechieVideoEditor/Models`); for the
multilingual models pick the language or let it be detected. **Make captions** transcribes
the selected clips (or every clip with sound) in the background with whisper.cpp on the GPU —
a few seconds of work per minute of speech — and puts them on a Captions track as title clips,
lines of at most 42 characters, two lines, split at pauses and sentence ends, each with its
words' timing (word times are aligned to the sound, so pauses fall between captions). Edit a
caption like any title; captions export with the video. **Import SRT or VTT…** and **Export
SRT… / VTT…** read and write subtitle files. Transcripts are kept per file, model and
language, so making captions again (after an edit) is instant.

**Caption styles and editing by transcript.** Captions keep each word's timing, so they can
follow the speech: **Highlight** colours the word being said, **Pop** also makes it grow,
**Karaoke** fills the words with colour as they are said, **Word by word** shows them as they
come (the Captions tab's Style row; the colour can be changed). Making captions again keeps the
style. The library's **Transcript** tab shows what was said in paragraphs, with timestamps;
click a word to go there, shift-click to select through another (⌘-click adds one), and
**Delete** (or ⌫) cuts those words out of their clips, closing the gaps; the captions are made
again to match, all in one undo step. **Remove fillers** cuts every "um", "uh" and the like
(transcription keeps them: it writes verbatim), and **Shorten pauses** trims pauses between
words longer than a length down to 0.3 s.

**Speech enhancement and noise reduction.** The Analyze tab's **Enhance speech** takes noise,
hum and room echo out of a clip's voices with DeepFilterNet3 (MIT / Apache-2.0) run by ONNX
Runtime, on this Mac: the first time it downloads the model (8 MB, from the DeepFilterNet
repository on GitHub, SHA-256 checked), then cleans the clip's whole file in the background
(with progress and Cancel; several times faster than real time). The clip then plays the
cleaned copy — same timing, at any speed, in the player and the export — kept in
`~/Library/Application Support/TechieVideoEditor/Enhanced` (`TVE_ENHANCED_DIR` moves it). **Lower
noise by up to** sets the strength (6–36 dB, or all of it); a new strength only re-mixes the
cleaned copy with the original, so it is quick. **Original sound** goes back; each is one undo
step. For live clean-up there are five Clean-up audio effects on RNNoise (BSD-3-Clause, model
data CC0): **Noise Reduction** (amount, and how much quieter between words), **Light Noise
Reduction**, **Quiet Between Words**, **Voice Cleanup** (with a low cut, presence and gentle
levelling) and **Podcast Voice** (also compressed and limited); they delay the clip's sound by
30 ms (a frame, plus RNNoise's own two). Stem separation (vocals / music) waits for a model
whose weights have a clear licence for use in an app.

**People.** For a clip of a video file, the Analyze tab finds people with Apple's Vision
framework, on this Mac, with nothing to download. **Portrait cutout** keeps the person and makes
the background transparent (the tracks below show through) — or, with **Remove the person
instead**, the reverse — found afresh in every frame as it is drawn, with edge softness and
an edge that shrinks or grows. **Find faces** looks through the whole file in the background
(kept per file, like the other analysis) and links what it sees into numbered faces with the
times they appear; choose the ones to hide (double-click one to go there) and **Cover faces**
with a **Mosaic**, **Blur** or **Fill** at a strength. The covers follow the faces, are saved
with the project (so they never depend on a cache), and can be restyled or removed in one
step. **Auto reframe** turns a landscape shot into **9:16**, **1:1**, **4:5** (or back to
**16:9**): the canvas takes that shape and the clip is scaled to fill it with position
keyframes that keep the subject — the faces, or what draws the eye when there are none — in
view, smoothed by **Smoothness**; canvas and clip are one undo step.

**Motion.** The Analyze tab's **Steady and clean** section measures a clip's camera motion and
brightness once (in the background; kept per file) with block matching of our own.
**Stabilize** smooths the camera's path by **Smoothness** — or, with **Lock the camera**, holds it
still like a tripod — and zooms in just enough to hide the edges (at most 30 %). **Remove flicker**
evens out brightness changes faster than a time you choose. **Denoise over time** averages grain
away where the picture is still, frame to frame, and leaves moving parts alone. Corrections are
saved with the project. **Track motion**: **Choose what to follow**, drag a box around it on the
player, then **Track object** (something that moves and changes shape) or **Track surface** (a flat
rectangle — a screen, a sign — which turns too); Vision follows it from the playhead to the clip's
end. Any clip on screen at the same time — a title, a sticker — can then **Follow** a track from
where it is at the playhead, with **Grow and shrink with it** and **Turn with it**, as keyframes you
can still edit.

**Screen and camera recording.** The timeline's screen button opens the recorder: choose a
display or a window (with its system sound), a camera, and the microphone, then **Record**. It
records from the playhead; **Stop** puts the screen, the camera (small in the bottom right corner,
picture in picture) and the microphone on new tracks there, as one undo step. Recordings are kept
with the voice-overs (`TVE_RECORDING_DIR` moves them). macOS asks once for Screen & System Audio
Recording and for the camera. The mouse is recorded too: the Analyze tab's **Cursor** section
draws rings where it clicked, a highlight around it or a spotlight that dims the rest (with a size),
**Zoom on clicks** zooms in on each click as keyframes, and **Click sounds** adds a click at each one
on a track of its own. `TVE_SCREEN_SOURCE=test` and `TVE_CAMERA_SOURCE=test` record from test
sources instead (moving bars, a tone, a cursor circling and clicking every second).

**Music and sound effects.** The library's **Audio** tab has our own music — 12 pieces in six
moods (calm, upbeat, cinematic, lo-fi, corporate, electronic) — and 15 sound effects (transitions,
hits, interface sounds, textures), all made by the app's own synthesiser from recipes in
`effects/sounds/`, so they are original and free to use. ▶ plays a preview (the same button stops
it); **+** or a double-click puts the sound at the playhead on the topmost audio track with room,
or on a new Music or Sound effects track. A sound is rendered once into
`~/Library/Application Support/TechieVideoEditor/Sounds` (`TVE_SOUND_CACHE_DIR`). **My music** shows
the audio files in folders you add (remembered in `music-folders.json`, `TVE_MUSIC_FOLDERS_FILE`).
**Add a sound pack…** adds a folder holding a pack: a `manifest.json` (format `tve-sound-pack`,
version 1) listing each sound's WAV file, kind, category, length, SHA-256 and an optional short
preview; a file that does not match its SHA-256 is left out and said so. Packs are kept in
`Sound Packs` (`TVE_SOUND_PACK_DIR`).

**Importing from Photos, Music and devices.** The Media tab's ⌄ button (or the links under an
empty bin) opens **Import from**: your Photos albums, your Music playlists, or a connected iPhone,
camera or memory card. Choose items and **Import**: photos, videos and device files are copied into
the project's `Imports` folder (beside the project, or `~/Movies/TechieVideoEditor Imports` before
it is saved; `TVE_IMPORT_DIR` moves it), named after the original and never overwriting; songs are
used where they are (songs in the cloud or copy-protected are not listed). macOS asks once to let
the app read Photos and Music. `TVE_PHOTOS_SOURCE`, `TVE_MUSIC_SOURCE` and `TVE_DEVICE_SOURCE` name
test folders to use instead (their sub-folders are the albums, playlists and devices).

**AI writing, translation and voices (your own keys).** The speech bubble at the top opens **AI
settings**: paste a Claude API key (for writing and translation) and an ElevenLabs key (for cloud
voices). Keys are kept in your Mac's Keychain, never in projects; what you use them on is sent to
Anthropic or ElevenLabs and billed to your accounts, and the dialog shows this session's tokens, an
estimated cost and the characters spoken. Claude Opus 5.5 writes by default (Claude Sonnet 5.5 is
faster and half the price). **Write with AI…** (Transcript tab) writes five titles — each can go on
the timeline — a description or a clean script from what is said (the transcript, or the captions),
streaming in as it is written; **Stop** ends it. **Translate** (Captions tab) puts the captions into
another language, each one keeping its time, as one undo step. The Audio tab's **Voice** speaks a
text with one of your Mac's voices (offline) or an ElevenLabs voice: **Listen**, then **Add at the
playhead** puts it on an audio track with captions timed to its words, as one undo step. Voices are
kept in a `Voices` folder beside the project (`TVE_VOICE_DIR`). Busy or rate-limited services are
tried again up to three times. `TVE_CLOUD_MOCK=1` runs everything against a local stand-in for both
services (no keys, nothing sent).

**Templates.** The library's **Templates** tab has eight original templates — Postcard, Birthday,
Launch, Story drop, Memories, Recipe, Game day and Slideshow — each a few media slots, titles and a
piece of our own music, with the cuts on the beat. Previews play when you point at them; star the
ones you like (**Favourites**). Choose one, pick its shape — 16:9, 9:16, 1:1, 4:3, 3:4 or 21:9 — and
**Use template** starts a new project from it. The tab then shows the storyboard: **Choose** what
goes in each slot from the media bin (or import a file) — it is cropped to fill the frame, and a
video that is too short is slowed to fit — move where a video starts, change the words, and pick
another piece of music or start it later. Every change can be undone. **Save as template…** turns
your own project into a template: the pictures you mark become slots, the titles you mark stay
editable, the sound you mark is the music, and everything else is copied in; your templates are kept
in `~/Library/Application Support/TechieVideoEditor/Templates` (`TVE_TEMPLATE_DIR`) as `.tvet`
folders and listed under **Mine**.

**Make it for me.** At the top of the Templates tab: **Instant video** takes pictures and videos
and a style — Cinematic, Upbeat, Calm, Energetic, Vintage or Clean, each with its own music,
transitions and look — and makes an edit of about the length you choose (15 s to 2 min) in any of
the six shapes, the best moments of each video cut to the beat, with an opening title if you type
one. **Highlights** does the same from videos only: their loudest, liveliest and most spoken moments.
**Shorts** takes one long video and makes up to five tall shorts of its best moments, each
following the subject and marked as a range, with captions when the video has a transcript;
**Export each short…** writes each to its own file. With a Claude key, **Let Claude choose** orders
the moments (and titles the shorts). Each makes a new project you can keep editing — the storyboard
can swap any shot.

**Drawing and paths.** The **Draw** button (the brush under the player) puts tools over the
picture: **Brush**, **Line**, **Arrow**, **Rectangle** and **Ellipse**, a colour, a width and a fill
for shapes. What you draw becomes a drawing clip at the playhead — more strokes go into it while it
is selected — and its inspector sets how long it takes to **draw on** and to **take off** (one path
after another, or all together) and lists its paths. Any clip with a picture can **follow a drawn
path** (Analyze tab): it moves from one end to the other over its length, and can turn along it. A
title can run **on a path** (its inspector): Arc, Dip, Wave, Circle, or a path you drew.

**Overlay transitions.** The Transitions tab's **Overlays** — warm and cool light leaks, a film
burn, white and black paint strokes, all made by the app — go over the cut nearest the playhead on
a track above, blended to suit (light leaks and the burn brighten the picture; paint covers the
cut). Each is a short movie with transparency, made the first time it is used and kept in
`~/Library/Application Support/TechieVideoEditor/Overlay cache` (`TVE_OVERLAY_CACHE_DIR`).

**Audio plug-ins.** Audio Units and VST3 effects installed on your Mac appear in the audio
effect picker (Inspector → Audio) as **Plug-in · name (maker, AU/VST3)**. A plug-in works like any
audio effect on a clip — on/off, removed, its settings on its card in the plug-in's own words, any
of them keyframed — and **Window** opens its own window; what you change there is one undo step.
Each track can have effects of its own too, on all of its sound: the **FX** button on its strip in
the mixer. A plug-in that isn't on this Mac (a project from elsewhere) is passed over, not lost.
VST3 bundles are checked once each in a separate process, so one that crashes can't take the app
with it; plug-ins that delay their sound are kept in sync with the picture.

**Video plug-ins.** OpenFX image effects installed on your Mac (in `/Library/OFX/Plugins`)
appear in the Effects tab's picker as **Plug-in · name**. Their settings show as our own controls —
sliders, switches, choices, colours; any number can be keyframed — and they run with Metal on the GPU
when the plug-in supports it, on the CPU otherwise. A plug-in works on the clip's own picture, before
its colour and our effects. Like audio plug-ins, each bundle is checked once in a separate process,
and one that isn't on this Mac is kept in the project and passed over.

**Colour and HDR.** A project works in SDR Rec.709 unless you choose **HDR · PQ** or **HDR · HLG**
(Rec.2020) under **Project colour** at the top of the Color tab. Every clip is converted into the
project's colour as it plays: HDR footage (an iPhone's HLG, a camera's PQ) is tone-mapped in an SDR
project, Display P3 and Rec.2020 media are converted, and SDR media, titles and stickers sit at
graphics white (203 nits) in an HDR one. The library marks HDR media (**HDR · PQ**, **HDR · HLG**).
The player always shows SDR; **HDR viewer** in the transport bar opens the picture in a window of
its own that shows it as HDR on an HDR (XDR) display, highlights rolled off to what the display
can show at its brightness now. An HDR project exported as HEVC is 10-bit HDR with its mastering
display and light levels; other formats come out SDR, tone-mapped. The scopes of an HDR project
read in nits.

**360° video.** Footage from a 360° camera is recognised from its metadata and marked **360°**
in the library. In a flat project a 360° clip is reframed: the Video tab's **360°** section sets
where it looks (**Direction**, **Tilt**, **Roll**) and how wide (**Field of view**) — each can be
keyframed to move the view — or drag in the player to look around and scroll to zoom. Put 360°
footage in an empty project and the project is 360° itself: the whole sphere, which the
player's **360°** button lets you look around in, and which exports as an MP4 marked as 360°
video for 360° players and sites. **Make a flat (16:9) project** turns it into a reframed video.

**Copilot.** The library's **Copilot** tab edits by asking, with your own Claude key: "cut it to
the best 30 seconds", "a title saying Welcome for the first three seconds", "cross-fade between
every shot", "warmer colour on the beach shots", "captions from what's said". Claude works on a
copy with the editor's own tools — trims, splits, moves and deletes, titles, transitions, effects,
colour and looks, captions — and lists what it would change; nothing happens until **Apply**, which
is one step you can undo. Each request sends Anthropic a description of the project (clip names
and timing, titles and captions, what is said in transcribed media), not the pictures or sound.

**Copied voices.** With an ElevenLabs key, **Copy a voice, with consent…** (Audio › Voice) makes a
copy of a voice for narration — only of the person reading, 18 or older, who agrees. They read a
consent statement written for them then (their name, today's date, four code words picked
afresh, a short passage) into the microphone; the recording is checked on this Mac with the
speech model, and only a recording that says the statement is sent to ElevenLabs. The voice then
appears among your voices, its consent recording kept with it; deleting it removes both.

**Smooth playback.** Stretches of the timeline too heavy to play in real time (many layers,
blurs, plug-ins, cutouts) show an orange bar along the ruler. **Render** in the timeline
toolbar (⌃R) renders them in the background — or the whole timeline — and they then play from
their render files (the bar turns blue). A render file belongs to exactly what its stretch
shows: change something there and the stretch is heavy again, undo and the file applies again.
Render files live in the app's data and are pruned past 20 GB; **Delete render files** removes
them. The player's **half resolution** button previews at a quarter of the pixels, which also
keeps heavy timelines smooth; exports are always at full size and never use render files.

**Languages.** The app speaks English and Tamil (தமிழ்). It follows your Mac's language; the
**Language** menu on the Home screen chooses one instead, and the change shows at once,
everywhere, without reopening anything. Every message, menu, undo step, effect, look, sound,
sticker and title template is translated, and numbers, dates and timecode are written the
language's way. Titles in Tamil are shaped whole (a consonant with its vowel sign stays one
letter, also when titles animate letter by letter). A few things stay as they are: names of
other products, file formats and codecs, and what is sent to cloud services (prompts, the
consent statement, which is read and checked in English). Translations live in
`i18n/tve_ta.ts` (Qt Linguist); `update_translations` gathers new text into it, and the
`tve_translations` test fails while any is untranslated.

**Other editors.** **Export timeline** in the top bar writes the edit for another editor:
**OpenTimelineIO** (DaVinci Resolve, Premiere Pro, Avid and most others), **Final Cut Pro XML**,
or an **EDL** of the main video track. **Open project…** opens OpenTimelineIO and Final Cut Pro XML
timelines as new projects. What carries over is the cut itself: the clips and their timing,
constant speed (reversed or frozen), cross dissolves, gaps, markers, and titles as placeholders
with their text. Colour, effects, keyframes and the like stay behind; after each export or
open a list says what didn't carry over, so you know what to redo. Media that isn't on this
Mac can be found again from the library.

**Store.** The library's **Store** tab lists packs of templates, music and sounds, stickers,
effects and transitions from the pack catalogue (by default on this repository's GitHub Pages;
the address is under **Catalogue address**). **Get** installs a pack, **Update** brings a newer
version (downloading only what changed) and **Remove** takes it away. A pack is installed only
if the catalogue is signed by a key the app trusts and every file is exactly the one published
(SHA-256); otherwise nothing changes. Installed sounds, templates and stickers appear in their
tabs at once; effects and transitions the next time the app opens. Packs are kept in
`~/Library/Application Support/TechieVideoEditor/Packs` (`TVE_PACKS_DIR`). Publishing packs —
the signing key, `tve-packtool` and signing — is described in
[tools/packs/README.md](tools/packs/README.md); no catalogue is published yet, and the app
trusts no key until the publisher adds theirs to `effects/store/trusted-keys.json`.

**Generate (your own keys).** The library's **Generate** tab makes media from a description:
**Picture** and **Sticker** with OpenAI GPT Image 2.5 (square, landscape or portrait; draft, good or
best; optionally working from pictures in the media bin), **Video** with Google's Gemini Omni Flash
(from words, from a picture, from a first and a last picture, or extending a video made here), and
**Music** and **Sound effect** with ElevenLabs (a length; instrumental; looping). Stickers come
with a transparent background — if one comes back without, its subject is cut out on your Mac —
and go small over the picture at the playhead; music and sound go at the playhead on an audio
track; pictures and video go into the media bin (**Made here** lists them, with **Add** and
**Extend**). Everything generated keeps its prompt and maker in the project and is marked **AI**
in the media bin. Files are kept in a `Generated` folder beside the project (`TVE_GENERATED_DIR`).
Add the OpenAI and Google AI (Gemini API) keys in **AI settings**; what you may do with the results
is set by each provider's terms, summarised under the Generate button (for example, ElevenLabs music
is not for film, TV, radio or studio games below its Enterprise Music plan, and prompts may not name
artists, songs or lyrics; Google's videos carry an invisible SynthID watermark).

**Advanced vision.** Select two or more clips of the same event (cameras, a sound recorder)
and **Line up by sound** in the Analyze tab: they are moved so their sound lines up (to about a
hundredth of a second), the first one selected staying put. **Cutout** also cuts out any subject:
**Draw around a subject** on the player and **Cut it out** — Vision follows it through the clip and
keeps what stands out under the box (an object, an animal). The **Picture** section makes a
processed copy of the clip's part of its file, which the clip then plays (kept in
`~/Library/Application Support/TechieVideoEditor/Processed`; `TVE_PROCESSED_DIR` moves it):
**Upscale** 2× or 4× with Apple's super-resolution (macOS downloads the model the first time;
pictures up to 1080p; slow), **Make frames** 2×, 4× or 8× for smooth slow motion (Apple's
frame-rate conversion), and **Remove an object**: draw around it, and it is followed through the
clip and filled in with what other frames show behind it, or from around it. **Original picture**
goes back; each is one undo step, and they can be stacked.

**Analyze.** The inspector's Analyze tab works on the selected clip (at normal speed) without
any AI model: it measures the media once in the background (with progress and Cancel; the
measurements are kept per file fingerprint in `~/Library/Caches/TechieVideoEditor/Analysis`,
`TVE_ANALYSIS_DIR` moves them, so later runs and new settings are instant), then shows what it
found on the timeline before anything changes.
- **Pauses** quieter than a level (dB) and longer than a length, minus a margin kept at each
  side, show as red bands; **Remove them** cuts them out and closes the gaps on that track in
  one undo step.
- **Scene changes** (colour histograms and pixel differences between frames, with a
  sensitivity) show as green lines; **Split there** or **Add markers**.
- **Beat**: the tempo (60–200 BPM) and a beat grid that follows the music (onset strength,
  tempo by autocorrelation, beat tracking by dynamic programming), as yellow ticks under the
  ruler; mark every beat or every fourth.
- **Lower under speech** turns the selected clip (music) down while the other clips playing
  with it have someone talking: volume keyframes dipping by an amount, with fades.

**Missing media.** Each imported file gets a fingerprint (its size and a hash of its first and
last 64 KB). When a project opens and some of its files are not where it says, a bar under
the top bar names them, their clips turn red and show nothing, and the media bin marks them
Missing. **Relink…** opens a dialog: **Search a folder…** looks through a folder and the
folders in it, by file name and — for renamed copies — by fingerprint, and relinks all it
finds in one undo step; **Locate…** points one item at a file by hand. A file whose content
differs from the one imported (another take, a re-export) needs a second yes ("Use anyway"),
and a file of another kind (sound for a video) is refused.

**Keyboard shortcuts.** The keyboard button in the top bar (or ⌘/) lists every editor action
with its key, by group, with a search. Click a key and press the new one (Esc cancels); a key
another action has asks before it moves; reset one or all. Tooltips show the keys in use.
Changes are kept for every project in `~/Library/Application Support/TechieVideoEditor/keymap.json`
(only what differs from the defaults; `TVE_KEYMAP_FILE` moves it).

**Right-click menus, tracks and the clipboard.** Right-click a clip for cut, copy, paste,
duplicate, delete or ripple delete, splitting or trimming at the playhead, detaching its audio,
its speed (¼× to 4×, reverse, freeze frame), grouping, compound clips, a marker, copying a
colour grade, and showing its file in Finder. Right-click a track's header to add a video,
audio or title track, rename, lock, hide or mute it, or delete it (or every empty track);
right-click empty timeline space to paste there or add a marker; right-click media in the
library to add it at the playhead, at the end, show it in Finder, or remove it from the
project. ⌘X, ⌘C, ⌘V, ⌘D, ⌘A and ⇧⌫ cut, copy, paste at the playhead, duplicate, select all and
ripple delete; pasted clips keep their spacing and tracks, moving up a track where something
is in the way.

**Timeline tools.** J, K and L shuttle: L plays, press again for 2×, 4×, 8×; J the same
backwards; K stops. Click the timecode under the player to type a time to go to. I and O mark
a range (⌥X clears it), shaded on the ruler: right-click the timeline to play it, delete it from
every track, and export it ("In to Out"). B is the blade: click a clip to cut it there (V goes
back to selecting). Right-click empty timeline space to close a gap, or every gap on a track,
and to make tracks small, normal or large. Copy a clip's attributes and paste them (⇧⌘V) onto
others, choosing position and size, colour, effects, keying, sound or speed; copy a
transition from its marker and paste it into another clip. Sound clips crossfade with a
transition between them. Speed runs from 0.01× to 100×.

**Source monitor, compound tabs, multicam.** ⌥-double-click a media item (or choose "Open in
the source monitor" from its menu) to play it on its own in the player's Source tab; mark In and
Out with I and O, then Insert it at the playhead (later clips move), Overwrite what is there, or
Append it after the last clip. Double-click a compound clip to edit its insides in a timeline tab
of its own; the tabs above the timeline take you back, the edits as one undo step. Import ▾ "A
project, as a compound clip…" brings another project in as one clip. For a shoot with several
cameras, line their clips up (Sync by sound), select them and make a multicam clip: press 1–9
(or click an angle in the Inspector) to cut to that camera at the playhead.

**Moving pictures.** A clip's anchor point (Transform, or ⌥-drag the cross in the player)
is what it scales and turns about. Turn on "Drag the corners in the player" under Corner pin to
move each corner on its own and stretch the picture in perspective, onto a screen in the shot,
say. Animation gives a video or photo a ready-made way in, out or through (fades, slides,
spins, pops, pulse, swing), and Pan and zoom moves slowly from one framing to another over
the whole clip; both are written as keyframes you can change. The library's colour-clip button
adds a clip of one colour or a gradient, and the camera button under the player saves the
frame at the playhead as a picture in the library.

**Masks, motion blur and guides.** A mask can apply to the picture (cutting it, as before), to
the clip's effects only, or to its colour only: black and white everywhere but a face, say, or
a grade on the sky alone. Motion blur (under Animation) smears a clip where its keyframes move
it, like a real camera's shutter. The grid button under the player shows thirds, centre lines,
safe areas and rulers, and the zoom menu beside it takes the player from Fit to 400 %.

**Colour tools.** On the Color tab, Auto colour takes a cast off the clip and fills its levels,
and Match makes its colours like another clip's (both set the red, green and blue curves, so
they can be adjusted). The HDR wheels colour and brighten six ranges of tones separately, from
the blacks to the specular highlights. Under Lens correction on the Video tab, pick an action
camera, phone, drone or fisheye lens to straighten its curved lines.

**Placing clips by hand.** Select a clip and its frame shows in the player: drag inside it to
move it (it snaps to the centre lines), a corner to resize it, the round knob to turn it (⇧ in
15° steps). Click a picture in the player to select its clip. Drag files from Finder onto the
timeline (they land where you drop them) or the player, and media from the library onto a
track or the player. The home screen's Record screen and Instant mode open a new project with
the recorder, or instant video, ready.

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
graded, given effects, faded and mixed like any clip (it is marked with the stack icon). It
also takes a speed like any clip — faster, slower, reversed, ramped, frozen, with its sound
stretched to keep its pitch or not — and transitions on the cuts into and out of it.
⌥⌘G on a compound clip breaks it apart, putting the part it plays back on the timeline (at
normal speed only: a sped-up or frozen one would no longer match what it showed).

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

**Audio effects, mixer and loudness.** The Audio tab's Audio effects section stacks 45 effects
of our own on a clip's sound: EQ (Graphic EQ, Bass Boost, Treble Boost, Voice Clarity, Warmth,
Low Cut, High Cut), dynamics (Compressor, Limiter, Noise Gate, Expander, Voice Leveler), rooms
(Reverb, Small Room, Large Room, Concert Hall, Church, Cave, Corridor, Bathroom, Stadium),
echoes (Echo, Ping-Pong Echo, Slapback), modulation (Chorus, Flanger, Stereo Widener,
Rotating), Pitch Shift, and voices (Deeper Voice, Higher Voice, Child, Chipmunk, Monster,
Robot, Radio, Telephone, Walkie-Talkie, Underwater, Megaphone), and clean-up (Noise Reduction,
Light Noise Reduction, Quiet Between Words, Voice Cleanup, Podcast Voice). They run after the clip's
volume and fades, and echoes and reverb ring on past its end. Each is a package of data
(`effects/audio`) chaining built-in processors — a ten-band EQ with 24 dB/octave cuts, a
compressor, a look-ahead limiter, a gate, an algorithmic reverb, delays, chorus, a widener,
auto-pan, a granular pitch shifter, distortion, a ring modulator, noise and RNNoise noise
reduction. Played faster or
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

**Export.** Click Export (⌘E). Pick a preset — 1080p or 4K for the web, a small file,
vertical 9:16 or square 1:1 for social apps (filled and cropped), a ProRes 422 HQ master, a
ProRes Proxy editing copy, an animated GIF, or the sound alone as AAC, WAV, FLAC or MP3 — or
choose each setting: the format (MP4 with H.264 or HEVC; MOV with H.264 or ProRes Proxy,
LT, 422, 422 HQ or 4444; GIF; M4A, 16- or 24-bit WAV, FLAC, or 320 kbit/s MP3), the size (the project's,
standard heights, vertical or square, or larger), the frame rate, the quality (H.264/HEVC),
whether to include the sound, loudness normalization, and the range: the whole timeline, the
selected clips, a marker's range, every marker range as a file of its own (named by the
notes), or from–to seconds. The dialog estimates the file size. **Export** shows the
progress; **Add to queue** goes back to editing. Exports run one after another in the
background from a snapshot of the project, so you can keep editing; the render queue in the
top bar lists them, with pause, resume, cancel (which removes the partial file), Show in
Finder and clear. Every frame is rendered with the same engine as the preview (transitions,
titles, effects, compound clips, sound) on an offscreen GPU device; H.264 and HEVC are
encoded by VideoToolbox from NV12 made on the GPU, ProRes by VideoToolbox (or FFmpeg's
encoder where the system has none), GIFs with a palette per frame and ordered dithering.
MP4, MOV and M4A get the chapter markers in the range as chapters, and MP3 as ID3 chapters.
MP3 is encoded by LAME (LGPL, built by vcpkg into FFmpeg), with its gapless header so players
keep the exact length. Imported ProRes 422 and 4444 now preview and show
thumbnails too. From the command line:

```bash
build/macos-release/app/TechieVideoEditor.app/Contents/MacOS/TechieVideoEditor project.tve --export out.mp4 --export-size 1920x1080 --export-codec h264
build/macos-release/app/TechieVideoEditor.app/Contents/MacOS/TechieVideoEditor project.tve --export master.mov --export-format mov-prores-hq
build/macos-release/app/TechieVideoEditor.app/Contents/MacOS/TechieVideoEditor project.tve --export clip.gif --export-preset gif
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

vcpkg builds FFmpeg (LGPL, with VideoToolbox and LAME 3.100 for MP3, LGPL), Catch2, nlohmann-json, miniaudio, ThorVG (MIT, for Lottie stickers), whisper.cpp with ggml (MIT, with its Metal backend, for speech to text),
RNNoise 0.2 (BSD-3-Clause, model data CC0; through an overlay port in `cmake/ports/rnnoise` that fetches the model
0.2 was released with — vcpkg's own port pairs it with a later one that barely reduces noise — and builds it with
CMake, so it builds on arm64 without autotools) and ONNX Runtime (MIT, built from source; its first build takes a while)
from `vcpkg.json`. The vcpkg `overrides` pin ONNX Runtime's dependencies to the checked-out baseline, so a shallow
vcpkg clone works.
Models are not in the repository: the app downloads the one you choose — Whisper (MIT) from the
official whisper.cpp model repository on Hugging Face, DeepFilterNet3 (MIT / Apache-2.0) from the
DeepFilterNet repository on GitHub — and checks its SHA-256 before using it.
Qt comes from Homebrew because building it through vcpkg takes hours.

## Build and test

```bash
export VCPKG_ROOT=~/vcpkg
cmake --preset macos-release -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build --preset macos-release
tools/make_test_media.sh          # synthetic test videos, a spoken sample (via `say`) and a public-domain NASA portrait (1.1 MB, SHA-256 checked) with clips made from it, and clips with known motion and sound offsets, in samples/
tools/fetch_models.sh             # the models the tests use (Whisper 57 MB, DeepFilterNet3 8 MB, SHA-256 checked) into samples/models/
ctest --preset macos-release
```

The suite includes `tve_golden_tests`: 531 fixed projects (every effect at its defaults and
changed, every transition at four moments, blend modes, placement, keyframes, colour, looks,
masks, keys, mattes, titles, stickers, compound clips, adjustment layers) built from generated
pictures, rendered by the real renderer offscreen and compared at 128 × 72 with the reference
PNGs in `tests/golden/metal/` (mean channel difference ≤ 2, at most 1 % of pixels far off).
`tve_perf_tests` measures the performance budgets (playback of three 4K layers with effects,
scrubbing with and without proxies, start-up, opening a 200-clip project, export speed,
memory) with the real engine and renderer, and fails when one is missed. They run one at a
time; on their own:

```bash
ctest --preset macos-release -L perf
```

After a deliberate change to how something looks, rewrite the references and review them:

```bash
TVE_GOLDEN_UPDATE=1 build/macos-release/tests/tve_golden_tests
```

A failing case leaves its picture, the reference and a difference image in
`$TMPDIR/tve_golden_failures/`.

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
