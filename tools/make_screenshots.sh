#!/bin/sh
# Retakes every screenshot in docs/images (the README, the design system and the web guide's
# pictures) from the app as it is now, in its dark and light themes, then rebuilds the web
# guide's images.
#
#   tools/make_screenshots.sh              every scene in tests/screens
#   tools/make_screenshots.sh titles 31d   only tests/screens/tst_titles.qml and tst_31d.qml
#
# Each tests/screens/tst_*.qml sets up the editor (media, clips, a tab, a dialog…) and saves
# docs/images/<name>-dark.png and -light.png. Needs a build (build/macos-release), the test
# media (tools/make_test_media.sh) and ffmpeg; the web images also need cwebp and Node.
set -eu
cd "$(dirname "$0")/.."

runner=build/macos-release/tests/tve_qml_tests
[ -x "$runner" ] || { echo "build the app first (cmake --build build/macos-release)" >&2; exit 1; }
[ -f samples/portrait.jpg ] || { echo "run tools/make_test_media.sh first" >&2; exit 1; }

# The media the scenes show, made from the test media.
media=samples/screens
mkdir -p "$media"
if [ ! -f "$media/night-street.mp4" ]; then
  ffmpeg -v error -y -loop 1 -framerate 30 -i samples/portrait.jpg -t 3 \
    -vf "scale=1920:-2,crop=1920:1080:0:ih*0.36,eq=brightness=-0.06:saturation=1.05,format=yuv420p" \
    -c:v libx264 -preset fast -crf 18 "$media/night-street.mp4"
fi
if [ ! -f "$media/portrait-wide.mp4" ]; then
  ffmpeg -v error -y -loop 1 -framerate 30 -i samples/portrait.jpg -t 2 \
    -vf "scale=1920:-2,crop=1920:1080:0:ih*0.12,format=yuv420p" \
    -c:v libx264 -preset fast -crf 18 "$media/portrait-wide.mp4"
fi

if [ ! -f "$media/photo.png" ]; then
  ffmpeg -v error -y -i samples/portrait.jpg -vf "scale=1600:-2,crop=1600:900:0:ih*0.1" "$media/photo.png"
fi
if [ ! -f "$media/photo.heic" ]; then
  sips -s format heic -Z 1400 samples/portrait.jpg --out "$media/photo.heic" > /dev/null
fi

if [ $# -gt 0 ]; then
  scenes=$(for name in "$@"; do echo "tests/screens/tst_$name.qml"; done)
else
  scenes=$(ls tests/screens/tst_*.qml)
fi

# The copilot scene asks in plain words; the test runner's mock Claude answers with this plan.
export TVE_MOCK_COPILOT_PLAN='[{"name":"add_title","input":{"text":"Welcome aboard","start":0,"duration":2.5}},
  {"name":"add_title","input":{"text":"Thanks for watching","start":4,"duration":2}}]'

log=$(mktemp)
trap 'rm -f "$log"' EXIT
failed=""
for scene in $scenes; do
  echo "screens: $scene"
  if ! TVE_SCREENS_OUT="$PWD/docs/images" TVE_SCREENS_MEDIA="$PWD/$media" \
       perl -e 'alarm 900; exec @ARGV' "$runner" -input "$scene" > "$log" 2>&1; then
    grep -E "^FAIL|Loc:" "$log" >&2 || true
    failed="$failed $scene"
  fi
done

sh tools/make_web_images.sh
if [ -n "$failed" ]; then
  echo "these scenes failed:$failed" >&2
  exit 1
fi
