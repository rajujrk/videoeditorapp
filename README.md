# TechieVideoEditor

A desktop video editor for macOS (Apple Silicon first). See
[docs/TechieVideoEditor_Build_Plan.md](docs/TechieVideoEditor_Build_Plan.md) for the full plan.

**Status: Phase 0 (foundations).** What exists today:

- `core/` — project model (tracks, clips, media), undoable edit commands (add, move, split,
  delete/ripple delete), JSON project save/load. Plain C++20, no Qt.
- `engine/` — FFmpeg decoder with VideoToolbox hardware decode and a software fallback,
  a background playback thread and a frame queue.
- `effects/` — built-in effect packages (manifest + GLSL shaders): color adjust, gaussian
  blur, vignette, RGB split, pixelate. See [effects/README.md](effects/README.md) for the
  manifest format and shader contract.
- `engine/gpu/` — effect library (manifests cross-checked against shader reflection) and
  the GPU effect chain on Qt RHI.
- `ui/player/` — `VideoView`, a Qt Quick item that shows decoded frames as Metal textures
  without copying them (CVPixelBuffer → CVMetalTexture → Qt RHI), converts YUV to RGB,
  runs the effect chain and aspect-fits the result.
- `app/` — a window that plays one video file, with an effects panel generated from the
  manifests and decode/present statistics.

## Requirements

- macOS 13+ on Apple Silicon, Xcode Command Line Tools
- CMake ≥ 3.25, Ninja, pkg-config, Qt 6.7+ (`brew install cmake ninja pkg-config qt`)
- vcpkg (`git clone https://github.com/microsoft/vcpkg ~/vcpkg && ~/vcpkg/bootstrap-vcpkg.sh`)

vcpkg builds FFmpeg (LGPL, with VideoToolbox), Catch2 and nlohmann-json from `vcpkg.json`.
Qt comes from Homebrew because building it through vcpkg takes hours.

## Build and test

```bash
export VCPKG_ROOT=~/vcpkg
cmake --preset macos-release -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build --preset macos-release
tools/make_test_media.sh          # synthetic test videos in samples/
ctest --preset macos-release
```

## Run the playback spike

```bash
build/macos-release/app/TechieVideoEditor.app/Contents/MacOS/TechieVideoEditor samples/4k60_h264.mp4
```

Or drop a video file onto the window. For an unattended measurement:

```bash
build/macos-release/app/TechieVideoEditor.app/Contents/MacOS/TechieVideoEditor --stats --quit-after 15 samples/4k60_h264.mp4
build/macos-release/app/TechieVideoEditor.app/Contents/MacOS/TechieVideoEditor --effects all --screenshot shot.png samples/4k60_h264.mp4
build/macos-release/tools/tve_decode_bench samples/4k60_h264.mp4
```

`--effects` takes `all` or comma-separated effect ids; `--screenshot` saves the window
after `--screenshot-at` seconds (default 3).

## Rules

- Never copy files from another product's app bundle into this repo (effects, LUTs, presets,
  models, fonts, sounds, templates). See §1 of the build plan.
- FFmpeg must stay an LGPL build; `tve_engine_tests` fails if a GPL build is linked.
